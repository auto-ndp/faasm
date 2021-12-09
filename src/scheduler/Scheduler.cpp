#include <faabric/proto/faabric.pb.h>
#include <faabric/redis/Redis.h>
#include <faabric/scheduler/ExecutorFactory.h>
#include <faabric/scheduler/FunctionCallClient.h>
#include <faabric/scheduler/Scheduler.h>
#include <faabric/snapshot/SnapshotClient.h>
#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/transport/PointToPointBroker.h>
#include <faabric/util/environment.h>
#include <faabric/util/func.h>
#include <faabric/util/locks.h>
#include <faabric/util/logging.h>
#include <faabric/util/memory.h>
#include <faabric/util/random.h>
#include <faabric/util/scheduling.h>
#include <faabric/util/snapshot.h>
#include <faabric/util/string_tools.h>
#include <faabric/util/testing.h>
#include <faabric/util/timing.h>

#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/syscall.h>

#include <chrono>
#include <unordered_set>

#define FLUSH_TIMEOUT_MS 10000
#define GET_EXEC_GRAPH_SLEEP_MS 500
#define MAX_GET_EXEC_GRAPH_RETRIES 3

using namespace faabric::util;
using namespace faabric::snapshot;

namespace faabric::scheduler {

// 0MQ sockets are not thread-safe, and opening them and closing them from
// different threads messes things up. However, we don't want to constatnly
// create and recreate them to make calls in the scheduler, therefore we cache
// them in TLS, and perform thread-specific tidy-up.
static thread_local std::unordered_map<std::string,
                                       faabric::scheduler::FunctionCallClient>
  functionCallClients;

static thread_local std::unordered_map<std::string,
                                       faabric::snapshot::SnapshotClient>
  snapshotClients;

MessageLocalResult::MessageLocalResult()
{
    event_fd = eventfd(0, EFD_CLOEXEC);
}

MessageLocalResult::~MessageLocalResult()
{
    if (event_fd >= 0) {
        close(event_fd);
    }
}

void MessageLocalResult::set_value(std::unique_ptr<faabric::Message>&& msg)
{
    this->promise.set_value(std::move(msg));
    eventfd_write(this->event_fd, (eventfd_t)1);
}

Scheduler& getScheduler()
{
    static Scheduler sch;
    return sch;
}

Scheduler::Scheduler()
  : thisHost(faabric::util::getSystemConfig().endpointHost)
  , conf(faabric::util::getSystemConfig())
  , broker(faabric::transport::getPointToPointBroker())
{
    // Set up the initial resources
    int cores = faabric::util::getUsableCores();
    thisHostResources.set_slots(cores);

    if (this->conf.isStorageNode) {
        redis::Redis& redis = redis::Redis::getQueue();
        redis.sadd(ALL_STORAGE_HOST_SET, this->thisHost);
    }

    if (!this->conf.schedulerMonitorFile.empty()) {
        this->monitorFd = open(conf.schedulerMonitorFile.c_str(),
                               O_RDWR | O_CREAT | O_NOATIME | O_TRUNC,
                               S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (this->monitorFd < 0) {
            perror("Couldn't open monitoring fd");
            this->monitorFd = -1;
        }
        this->updateMonitoring();
    }
}

std::set<std::string> Scheduler::getAvailableHosts()
{
    ZoneScopedN("Scheduler::getAvailableHosts");
    redis::Redis& redis = redis::Redis::getQueue();
    return redis.smembers(getGlobalSetName());
}

std::set<std::string> Scheduler::getAvailableHostsForFunction(
  const faabric::Message& msg)
{
    ZoneScopedN("Scheduler::getAvailableHostsForFunction");
    redis::Redis& redis = redis::Redis::getQueue();
    return redis.smembers(getGlobalSetNameForFunction(msg));
}

void Scheduler::addHostToGlobalSet(const std::string& host)
{
    redis::Redis& redis = redis::Redis::getQueue();
    redis.sadd(getGlobalSetName(), host);
}

void Scheduler::removeHostFromGlobalSet(const std::string& host)
{
    redis::Redis& redis = redis::Redis::getQueue();
    redis.srem(getGlobalSetName(), host);
}

void Scheduler::addHostToGlobalSet()
{
    this->addHostToGlobalSet(thisHost);
}

const char* Scheduler::getGlobalSetName() const
{
    return this->conf.isStorageNode ? AVAILABLE_STORAGE_HOST_SET
                                    : AVAILABLE_HOST_SET;
}

const char* Scheduler::getGlobalSetNameForFunction(
  const faabric::Message& msg) const
{
    return msg.isstorage() ? AVAILABLE_STORAGE_HOST_SET : AVAILABLE_HOST_SET;
}

void Scheduler::resetThreadLocalCache()
{
    auto tid = (pid_t)syscall(SYS_gettid);
    SPDLOG_DEBUG("Resetting scheduler thread-local cache for thread {}", tid);

    functionCallClients.clear();
    snapshotClients.clear();
}

void Scheduler::reset()
{
    SPDLOG_DEBUG("Resetting scheduler");

    resetThreadLocalCache();

    // Shut down all Executors
    // Executor shutdown takes a lock itself, so "finish" executors without the
    // lock.
    decltype(executors) executorsList;
    decltype(deadExecutors) deadExecutorsList;
    {
        faabric::util::FullLock lock(mx);
        executorsList = executors;
    }
    for (auto& p : executorsList) {
        for (auto& e : p.second) {
            e->finish();
        }
    }
    executorsList.clear();
    {
        faabric::util::FullLock lock(mx);
        deadExecutorsList = deadExecutors;
    }
    for (auto& e : deadExecutors) {
        e->finish();
    }
    deadExecutorsList.clear();

    faabric::util::FullLock lock(mx);
    executors.clear();
    deadExecutors.clear();

    // Ensure host is set correctly
    thisHost = faabric::util::getSystemConfig().endpointHost;

    // Reset resources
    thisHostResources = faabric::HostResources();
    thisHostResources.set_slots(faabric::util::getUsableCores());

    // Reset scheduler state
    availableHostsCache.clear();
    registeredHosts.clear();
    threadResults.clear();
    pushedSnapshotsMap.clear();

    // Records
    recordedMessagesAll.clear();
    recordedMessagesLocal.clear();
    recordedMessagesShared.clear();
}

void Scheduler::shutdown()
{
    reset();

    this->removeHostFromGlobalSet(thisHost);
    if (this->conf.isStorageNode) {
        redis::Redis& redis = redis::Redis::getQueue();
        redis.srem(ALL_STORAGE_HOST_SET, thisHost);
    }
}

long Scheduler::getFunctionExecutorCount(const faabric::Message& msg)
{
    faabric::util::SharedLock lock(mx);
    const std::string funcStr = faabric::util::funcToString(msg, false);
    return executors[funcStr].size();
}

int Scheduler::getFunctionRegisteredHostCount(const faabric::Message& msg)
{
    faabric::util::SharedLock lock(mx);
    const std::string funcStr = faabric::util::funcToString(msg, false);
    return (int)registeredHosts[funcStr].size();
}

std::set<std::string> Scheduler::getFunctionRegisteredHosts(
  const faabric::Message& msg,
  bool acquireLock)
{
    faabric::util::SharedLock lock;
    if (acquireLock) {
        lock = faabric::util::SharedLock(mx);
    }
    const std::string funcStr = faabric::util::funcToString(msg, false);
    return registeredHosts[funcStr];
}

void Scheduler::removeRegisteredHost(const std::string& host,
                                     const faabric::Message& msg)
{
    faabric::util::FullLock lock(mx);
    const std::string funcStr = faabric::util::funcToString(msg, false);
    registeredHosts[funcStr].erase(host);
}

void Scheduler::vacateSlot()
{
    ZoneScopedNS("Vacate scheduler slot", 5);
    thisHostUsedSlots.fetch_sub(1, std::memory_order_acq_rel);
}

void Scheduler::notifyExecutorShutdown(Executor* exec,
                                       const faabric::Message& msg)
{
    ZoneScopedNS("Scheduler::notifyExecutorShutdown", 5);
    faabric::util::FullLock lock(mx);

    SPDLOG_TRACE("Shutting down executor {}", exec->id);

    std::string funcStr = faabric::util::funcToString(msg, false);

    // Find in list of executors
    int execIdx = -1;
    std::vector<std::shared_ptr<Executor>>& thisExecutors = executors[funcStr];
    for (int i = 0; i < thisExecutors.size(); i++) {
        if (thisExecutors.at(i).get() == exec) {
            execIdx = i;
            break;
        }
    }

    if (execIdx < 0) {
        SPDLOG_ERROR(
          "Couldn't find executor with id {}, current executor count: {}",
          exec->id,
          thisExecutors.size());
        return;
    }
    // We assume it's been found or something has gone very wrong
    assert(execIdx >= 0);

    // Record as dead, remove from live executors
    // Note that this is necessary as this method may be called from a worker
    // thread, so we can't fully clean up the executor without having a deadlock
    deadExecutors.emplace_back(thisExecutors.at(execIdx));
    thisExecutors.erase(thisExecutors.begin() + execIdx);

    if (thisExecutors.empty()) {
        SPDLOG_TRACE("No remaining executors for {}", funcStr);

        // Unregister if this was the last executor for that function
        bool isMaster = thisHost == msg.masterhost();
        if (!isMaster) {
            faabric::UnregisterRequest req;
            req.set_host(thisHost);
            *req.mutable_function() = msg;

            getFunctionCallClient(msg.masterhost()).unregister(req);
        }
    }
}

faabric::util::SchedulingDecision Scheduler::callFunctions(
  std::shared_ptr<faabric::BatchExecuteRequest> req,
  faabric::util::SchedulingTopologyHint topologyHint,
  faabric::Message* caller)
{
    ZoneScopedNS("Scheduler::callFunctions", 5);
    auto& config = faabric::util::getSystemConfig();

    // Note, we assume all the messages are for the same function and have the
    // same master host
    faabric::Message& firstMsg = req->mutable_messages()->at(0);
    std::string masterHost = firstMsg.masterhost();
    if (masterHost.empty()) {
        std::string funcStrWithId = faabric::util::funcToString(firstMsg, true);
        SPDLOG_ERROR("Request {} has no master host", funcStrWithId);
        throw std::runtime_error("Message with no master host");
    }

    // If we're not the master host, we need to forward the request back to the
    // master host. This will only happen if a nested batch execution happens.
    if (topologyHint != faabric::util::SchedulingTopologyHint::FORCE_LOCAL &&
        masterHost != thisHost) {
        std::string funcStr = faabric::util::funcToString(firstMsg, false);
        SPDLOG_DEBUG("Forwarding {} back to master {}", funcStr, masterHost);

        ZoneScopedN("Scheduler::callFunctions forward to master");
        getFunctionCallClient(masterHost).executeFunctions(req);
        SchedulingDecision decision(firstMsg.appid(), firstMsg.groupid());
        decision.returnHost = masterHost;
        return decision;
    }

    faabric::util::FullLock lock(mx);

    SchedulingDecision decision = makeSchedulingDecision(req, topologyHint);

    // Send out point-to-point mappings if necessary (unless being forced to
    // execute locally, in which case they will be transmitted from the
    // master)
    if (topologyHint != faabric::util::SchedulingTopologyHint::FORCE_LOCAL &&
        (firstMsg.groupid() > 0)) {
        broker.setAndSendMappingsFromSchedulingDecision(decision);
    }

    // Pass decision as hint
    return doCallFunctions(req, decision, caller, lock);
}

faabric::util::SchedulingDecision Scheduler::makeSchedulingDecision(
  std::shared_ptr<faabric::BatchExecuteRequest> req,
  faabric::util::SchedulingTopologyHint topologyHint)
{
    int nMessages = req->messages_size();
    faabric::Message& firstMsg = req->mutable_messages()->at(0);
    std::string funcStr = faabric::util::funcToString(firstMsg, false);

    bool isStorage = firstMsg.isstorage();
    bool iAmStorage = conf.isStorageNode;
    bool hostKindDifferent = (isStorage != iAmStorage);

    // If topology hints are disabled, unset the provided topology hint
    if (conf.noTopologyHints == "on" &&
        topologyHint != faabric::util::SchedulingTopologyHint::NORMAL) {
        SPDLOG_WARN("Ignoring topology hint passed to scheduler as hints are "
                    "disabled in the config");
        topologyHint = faabric::util::SchedulingTopologyHint::NORMAL;
    }

    std::vector<std::string> hosts;
    if (topologyHint == faabric::util::SchedulingTopologyHint::FORCE_LOCAL) {
        // We're forced to execute locally here so we do all the messages
        for (int i = 0; i < nMessages; i++) {
            hosts.push_back(thisHost);
        }
    } else {
        // At this point we know we're the master host, and we've not been
        // asked to force full local execution.

        // Work out how many we can handle locally
        int slots = thisHostResources.slots();

        // Work out available cores, flooring at zero
        int available =
          slots - this->thisHostUsedSlots.load(std::memory_order_acquire);
        available = std::max<int>(available, 0);

        // Claim as many as we can
        int nLocally = std::min<int>(available, nMessages);

        // Make sure we don't execute the wrong kind (storage/compute) of
        // call locally
        if (hostKindDifferent) {
            nLocally = 0;
        }

        // Add those that can be executed locally
        for (int i = 0; i < nLocally; i++) {
            hosts.push_back(thisHost);
        }

        // If some are left, we need to distribute.
        // First try and do so on already registered hosts.
        int remainder = nMessages - nLocally;

        if (!hostKindDifferent && remainder > 0) {
            std::set<std::string> thisRegisteredHosts =
              registeredHosts[funcStr];

            for (const auto& h : thisRegisteredHosts) {
                // Work out resources on this host
                faabric::HostResources r = getHostResources(h);
                int available = r.slots() - r.usedslots();
                int nOnThisHost = std::min(available, remainder);

                // Under the NEVER_ALONE topology hint, we never choose a
                // host unless we can schedule at least two requests in it.
                if (topologyHint ==
                      faabric::util::SchedulingTopologyHint::NEVER_ALONE &&
                    nOnThisHost < 2) {
                    continue;
                }

                for (int i = 0; i < nOnThisHost; i++) {
                    hosts.push_back(h);
                }

                remainder -= nOnThisHost;
                if (remainder <= 0) {
                    break;
                }
            }
        }

        // Now schedule to unregistered hosts if there are messages left
        std::string lastHost;
        if (remainder > 0) {
            std::vector<std::string> unregisteredHosts;
            if (hostKindDifferent) {
                for (auto&& h : getAvailableHostsForFunction(firstMsg)) {
                    unregisteredHosts.push_back(std::move(h));
                }
            } else {
                unregisteredHosts = getUnregisteredHosts(funcStr);
            }

            for (const auto& h : unregisteredHosts) {
                // Skip if this host
                if (h == thisHost) {
                    continue;
                }

                lastHost = h;

                // Work out resources on this host
                faabric::HostResources r = getHostResources(h);
                int available = r.slots() - r.usedslots();
                int nOnThisHost = std::min(available, remainder);

                if (topologyHint ==
                      faabric::util::SchedulingTopologyHint::NEVER_ALONE &&
                    nOnThisHost < 2) {
                    continue;
                }

                // Register the host if it's exected a function
                if (nOnThisHost > 0) {
                    registeredHosts[funcStr].insert(h);
                }

                for (int i = 0; i < nOnThisHost; i++) {
                    hosts.push_back(h);
                }

                remainder -= nOnThisHost;
                if (remainder <= 0) {
                    break;
                }
            }
        }

        // At this point there's no more capacity in the system, so we
        // just need to overload locally
        if (remainder > 0) {
            std::string overloadedHost =
              hostKindDifferent ? lastHost : thisHost;

            // Under the NEVER_ALONE scheduling topology hint we want to
            // overload the last host we assigned requests to.
            if (topologyHint ==
                  faabric::util::SchedulingTopologyHint::NEVER_ALONE &&
                !hosts.empty()) {
                overloadedHost = hosts.back();
            }

            SPDLOG_DEBUG("Overloading {}/{} {} {}",
                         remainder,
                         nMessages,
                         funcStr,
                         overloadedHost == thisHost
                           ? "locally"
                           : "to host " + overloadedHost);

            for (int i = 0; i < remainder; i++) {
                hosts.push_back(overloadedHost);
            }
        }
    }

    // Sanity check
    assert(hosts.size() == nMessages);

    // Set up decision
    SchedulingDecision decision(firstMsg.appid(), firstMsg.groupid());
    for (int i = 0; i < hosts.size(); i++) {
        decision.addMessage(hosts.at(i), req->messages().at(i));
    }

    return decision;
}

faabric::util::SchedulingDecision Scheduler::callFunctions(
  std::shared_ptr<faabric::BatchExecuteRequest> req,
  faabric::util::SchedulingDecision& hint,
  faabric::Message* caller)
{
    faabric::util::FullLock lock(mx);
    return doCallFunctions(req, hint, caller, lock);
}

faabric::util::SchedulingDecision Scheduler::doCallFunctions(
  std::shared_ptr<faabric::BatchExecuteRequest> req,
  faabric::util::SchedulingDecision& decision,
  faabric::Message* caller,
  faabric::util::FullLock& lock)
{
    faabric::Message& firstMsg = req->mutable_messages()->at(0);
    std::string funcStr = faabric::util::funcToString(firstMsg, false);
    int nMessages = req->messages_size();

    if (decision.hosts.size() != nMessages) {
        SPDLOG_ERROR(
          "Passed decision for {} with {} messages, but request has {}",
          funcStr,
          decision.hosts.size(),
          nMessages);
        throw std::runtime_error("Invalid scheduler hint for messages");
    }

    // NOTE: we want to schedule things on this host _last_, otherwise
    // functions may start executing before all messages have been
    // dispatched, thus slowing the remaining scheduling. Therefore we want
    // to create a list of unique hosts, with this host last.
    std::vector<std::string> orderedHosts;
    {
        std::set<std::string> uniqueHosts(decision.hosts.begin(),
                                          decision.hosts.end());
        bool hasFunctionsOnThisHost = uniqueHosts.contains(thisHost);

        if (hasFunctionsOnThisHost) {
            uniqueHosts.erase(thisHost);
        }

        orderedHosts = std::vector(uniqueHosts.begin(), uniqueHosts.end());

        if (hasFunctionsOnThisHost) {
            orderedHosts.push_back(thisHost);
        }
    }

    // -------------------------------------------
    // THREADS
    // -------------------------------------------
    bool isThreads = req->type() == faabric::BatchExecuteRequest::THREADS;

    // Register thread results if necessary
    if (isThreads) {
        for (const auto& m : req->messages()) {
            registerThread(m.id());
        }
    }

    // -------------------------------------------
    // SNAPSHOTS
    // -------------------------------------------

    // Push out snapshot diffs to registered hosts. We have to do this
    // to *all* hosts, regardless of whether they will be executing
    // functions. This greatly simplifies the reasoning about which
    // hosts hold which diffs.
    std::string snapshotKey = firstMsg.snapshotkey();
    if (!snapshotKey.empty()) {
        for (const auto& host : getFunctionRegisteredHosts(firstMsg, false)) {
            SnapshotClient& c = getSnapshotClient(host);
            auto snapshotData =
              faabric::snapshot::getSnapshotRegistry().getSnapshot(snapshotKey);

            // See if we've already pushed this snapshot to the given host,
            // if so, just push the diffs
            if (pushedSnapshotsMap[snapshotKey].contains(host)) {
                std::vector<faabric::util::SnapshotDiff> snapshotDiffs =
                  snapshotData->getDirtyPages();
                c.pushSnapshotDiffs(
                  snapshotKey, firstMsg.groupid(), snapshotDiffs);
            } else {
                c.pushSnapshot(snapshotKey, firstMsg.groupid(), *snapshotData);
                pushedSnapshotsMap[snapshotKey].insert(host);
            }
        }
    }

    // Now reset the dirty page tracking just before we start executing
    SPDLOG_DEBUG("Resetting dirty tracking after pushing diffs {}", funcStr);
    faabric::util::resetDirtyTracking();

    // -------------------------------------------
    // EXECTUION
    // -------------------------------------------

    // Records for tests - copy messages before execution to avoid
    // racing on msg
    size_t recordedMessagesOffset = recordedMessagesAll.size();
    if (faabric::util::isTestMode()) {
        for (int i = 0; i < nMessages; i++) {
            recordedMessagesAll.emplace_back(req->messages().at(i));
        }
    }

    // Iterate through unique hosts and dispatch messages
    for (const std::string& host : orderedHosts) {
        // Work out which indexes are scheduled on this host
        std::vector<int> thisHostIdxs;
        for (int i = 0; i < decision.hosts.size(); i++) {
            if (decision.hosts.at(i) == host) {
                thisHostIdxs.push_back(i);
            }
        }

        if (host == thisHost) {
            // -------------------------------------------
            // LOCAL EXECTUION
            // -------------------------------------------
            // For threads we only need one executor, for anything else
            // we want one Executor per function in flight.

            if (thisHostIdxs.empty()) {
                SPDLOG_DEBUG("Not scheduling any calls to {} out of {} locally",
                             funcStr,
                             nMessages);
                continue;
            }

            SPDLOG_DEBUG("Scheduling {}/{} calls to {} locally",
                         thisHostIdxs.size(),
                         nMessages,
                         funcStr);

            // Update slots
            this->thisHostUsedSlots.fetch_add(thisHostIdxs.size(),
                                              std::memory_order_acquire);

            if (isThreads) {
                // Threads use the existing executor. We assume there's
                // only one running at a time.
                std::vector<std::shared_ptr<Executor>>& thisExecutors =
                  executors[funcStr];

                std::shared_ptr<Executor> e = nullptr;
                if (thisExecutors.empty()) {
                    ZoneScopedN(
                      "Scheduler::callFunctions claiming new executor");
                    // Create executor if not exists
                    e = claimExecutor(firstMsg);
                } else if (thisExecutors.size() == 1) {
                    // Use existing executor if exists
                    e = thisExecutors.back();
                } else {
                    SPDLOG_ERROR("Found {} executors for threaded function {}",
                                 thisExecutors.size(),
                                 funcStr);
                    throw std::runtime_error(
                      "Expected only one executor for threaded "
                      "function");
                }

                assert(e != nullptr);

                // Execute the tasks
                e->executeTasks(thisHostIdxs, req);
            } else {
                // Non-threads require one executor per task
                for (auto i : thisHostIdxs) {
                    faabric::Message& localMsg = req->mutable_messages()->at(i);

                    if (localMsg.directresulthost() == conf.endpointHost) {
                        localMsg.set_directresulthost("");
                    }

                    if (localMsg.executeslocally()) {
                        faabric::util::UniqueLock resultsLock(
                          localResultsMutex);
                        localResults.insert(
                          { localMsg.id(),
                            std::make_shared<MessageLocalResult>() });
                    }

                    std::shared_ptr<Executor> e = claimExecutor(localMsg);
                    e->executeTasks({ i }, req);
                }
            }
        } else {
            // -------------------------------------------
            // REMOTE EXECTUION
            // -------------------------------------------

            SPDLOG_DEBUG("Scheduling {}/{} calls to {} on {}",
                         thisHostIdxs.size(),
                         nMessages,
                         funcStr,
                         host);

            // Set up new request
            std::shared_ptr<faabric::BatchExecuteRequest> hostRequest =
              faabric::util::batchExecFactory();
            hostRequest->set_snapshotkey(req->snapshotkey());
            hostRequest->set_type(req->type());
            hostRequest->set_subtype(req->subtype());
            hostRequest->set_contextdata(req->contextdata());

            // Add messages
            for (auto msgIdx : thisHostIdxs) {
                auto* newMsg = hostRequest->add_messages();
                *newMsg = req->messages().at(msgIdx);
                newMsg->set_executeslocally(false);
                if (!newMsg->directresulthost().empty()) {
                    faabric::util::UniqueLock resultsLock(localResultsMutex);
                    localResults.insert(
                      { newMsg->id(), std::make_shared<MessageLocalResult>() });
                }
            }

            // Dispatch the calls
            getFunctionCallClient(host).executeFunctions(hostRequest);
        }
    }

    // Records for tests
    if (faabric::util::isTestMode()) {
        for (int i = 0; i < nMessages; i++) {
            std::string executedHost = decision.hosts.at(i);
            const faabric::Message& msg =
              recordedMessagesAll.at(recordedMessagesOffset + i);

            // Log results if in test mode
            if (executedHost.empty() || executedHost == thisHost) {
                recordedMessagesLocal.emplace_back(msg);
            } else {
                recordedMessagesShared.emplace_back(executedHost, msg);
            }
        }
    }

    return decision;
}

std::vector<std::string> Scheduler::getUnregisteredHosts(
  const std::string& funcStr,
  bool noCache)
{
    // Load the list of available hosts
    if (availableHostsCache.empty() || noCache) {
        availableHostsCache = getAvailableHosts();
    }

    // At this point we know we need to enlist unregistered hosts
    std::set<std::string>& thisRegisteredHosts = registeredHosts[funcStr];

    std::vector<std::string> unregisteredHosts;

    std::set_difference(
      availableHostsCache.begin(),
      availableHostsCache.end(),
      thisRegisteredHosts.begin(),
      thisRegisteredHosts.end(),
      std::inserter(unregisteredHosts, unregisteredHosts.begin()));

    // If we've not got any, try again without caching
    if (unregisteredHosts.empty() && !noCache) {
        return getUnregisteredHosts(funcStr, true);
    }

    return unregisteredHosts;
}

void Scheduler::broadcastSnapshotDelete(const faabric::Message& msg,
                                        const std::string& snapshotKey)
{
    std::string funcStr = faabric::util::funcToString(msg, false);
    std::set<std::string>& thisRegisteredHosts = registeredHosts[funcStr];

    for (auto host : thisRegisteredHosts) {
        SnapshotClient& c = getSnapshotClient(host);
        c.deleteSnapshot(snapshotKey);
    }
}

void Scheduler::callFunction(faabric::Message& msg,
                             bool forceLocal,
                             faabric::Message* caller)
{
    // TODO - avoid this copy
    auto req = faabric::util::batchExecFactory();
    *req->add_messages() = msg;

    // Specify that this is a normal function, not a thread
    req->set_type(req->FUNCTIONS);

    // Make the call
    if (forceLocal) {
        callFunctions(
          req, faabric::util::SchedulingTopologyHint::FORCE_LOCAL, caller);
    } else {
        callFunctions(
          req, faabric::util::SchedulingTopologyHint::NORMAL, caller);
    }
}

void Scheduler::clearRecordedMessages()
{
    faabric::util::FullLock lock(mx);
    recordedMessagesAll.clear();
    recordedMessagesLocal.clear();
    recordedMessagesShared.clear();
}

std::vector<faabric::Message> Scheduler::getRecordedMessagesAll()
{
    faabric::util::SharedLock lock(mx);
    return recordedMessagesAll;
}

std::vector<faabric::Message> Scheduler::getRecordedMessagesLocal()
{
    faabric::util::SharedLock lock(mx);
    return recordedMessagesLocal;
}

FunctionCallClient& Scheduler::getFunctionCallClient(
  const std::string& otherHost)
{
    if (functionCallClients.find(otherHost) == functionCallClients.end()) {
        SPDLOG_DEBUG("Adding new function call client for {}", otherHost);
        functionCallClients.emplace(otherHost, otherHost);
    }

    return functionCallClients.at(otherHost);
}

SnapshotClient& Scheduler::getSnapshotClient(const std::string& otherHost)
{
    if (snapshotClients.find(otherHost) == snapshotClients.end()) {
        SPDLOG_DEBUG("Adding new snapshot client for {}", otherHost);
        snapshotClients.emplace(otherHost, otherHost);
    }

    return snapshotClients.at(otherHost);
}

std::vector<std::pair<std::string, faabric::Message>>
Scheduler::getRecordedMessagesShared()
{
    faabric::util::SharedLock lock(mx);
    return recordedMessagesShared;
}

std::shared_ptr<Executor> Scheduler::claimExecutor(faabric::Message& msg)
{
    std::string funcStr = faabric::util::funcToString(msg, false);

    std::vector<std::shared_ptr<Executor>>& thisExecutors = executors[funcStr];

    if (thisExecutors.empty()) {
        suspendedExecutors[funcStr] = 0;
    }

    std::shared_ptr<Executor> claimed = nullptr;
    for (auto& e : thisExecutors) {
        if (e->tryClaim()) {
            claimed = e;
            SPDLOG_DEBUG(
              "Reusing warm executor {} for {}", claimed->id, funcStr);
            break;
        }
    }

    // We have no warm executors available, so scale up
    if (claimed == nullptr) {
        int nExecutors = thisExecutors.size();
        int nSuspended = suspendedExecutors[funcStr];
        // allow for 2 threads per available core, 12 threads in case of
        // suspended threads
        int maxSubscription = 2 * std::thread::hardware_concurrency();
        if (nExecutors - std::min(nSuspended, maxSubscription * 6) >
            std::max(1, maxSubscription)) {
            ZoneScopedN("Scheduler::claimExecutor oversubscribed");
            // oversubscribed, enqueue onto one of the other executors
            int minQueueSize = thisExecutors.at(0)->getQueueLength();
            int minQueueIdx = 0;
            for (int i = 1; i < nExecutors && minQueueSize > 0; i++) {
                int qs = thisExecutors.at(i)->getQueueLength();
                if (qs < minQueueSize) {
                    minQueueSize = qs;
                    minQueueIdx = i;
                }
            }
            SPDLOG_DEBUG("Queueing {} onto oversubscribed executor {}",
                         funcStr,
                         minQueueIdx);
            claimed = thisExecutors.at(minQueueIdx);
        } else {
            ZoneScopedN("Scheduler::claimExecutor scaling up");
            SPDLOG_DEBUG(
              "Scaling {} from {} -> {}", funcStr, nExecutors, nExecutors + 1);
            std::shared_ptr<faabric::scheduler::ExecutorFactory> factory =
              getExecutorFactory();
            auto executor = factory->createExecutor(msg);
            thisExecutors.push_back(std::move(executor));
            claimed = thisExecutors.back();

            // Claim it
            claimed->tryClaim();
        }
    }

    assert(claimed != nullptr);
    return claimed;
}

std::string Scheduler::getThisHost()
{
    faabric::util::SharedLock lock(mx);
    return thisHost;
}

void Scheduler::broadcastFlush()
{
    faabric::util::FullLock lock(mx);
    // Get all hosts
    redis::Redis& redis = redis::Redis::getQueue();
    std::set<std::string> allHosts = redis.smembers(AVAILABLE_HOST_SET);
    allHosts.merge(redis.smembers(AVAILABLE_STORAGE_HOST_SET));

    // Remove this host from the set
    allHosts.erase(thisHost);

    // Dispatch flush message to all other hosts
    for (auto& otherHost : allHosts) {
        getFunctionCallClient(otherHost).sendFlush();
    }

    lock.unlock();
    flushLocally();
}

void Scheduler::flushLocally()
{
    SPDLOG_INFO("Flushing host {}",
                faabric::util::getSystemConfig().endpointHost);

    // Reset this scheduler
    reset();

    // Flush the host
    getExecutorFactory()->flushHost();
}

void Scheduler::setFunctionResult(faabric::Message& msg)
{
    ZoneScopedNS("Scheduler::setFunctionResult", 5);
    ZoneValue(msg.ByteSizeLong());

    const auto& myHostname = faabric::util::getSystemConfig().endpointHost;

    const auto& directResultHost = msg.directresulthost();
    if (directResultHost == myHostname) {
        faabric::util::UniqueLock resultsLock(localResultsMutex);
        auto it = localResults.find(msg.id());
        if (it != localResults.end()) {
            it->second->set_value(std::make_unique<faabric::Message>(msg));
        } else {
            throw std::runtime_error(
              "Got direct result, but promise is registered");
        }
        return;
    }

    // Record which host did the execution
    msg.set_executedhost(myHostname);

    // Set finish timestamp
    msg.set_finishtimestamp(faabric::util::getGlobalClock().epochMillis());

    if (!directResultHost.empty()) {
        ZoneScopedN("Direct result send");
        faabric::util::FullLock lock(mx);
        auto& fc = getFunctionCallClient(directResultHost);
        lock.unlock();
        {
            ZoneScopedN("Socket send");
            fc.sendDirectResult(msg);
        }
        return;
    }

    if (msg.executeslocally()) {
        ZoneScopedN("Local results publish");
        faabric::util::UniqueLock resultsLock(localResultsMutex);

        auto it = localResults.find(msg.id());
        if (it != localResults.end()) {
            it->second->set_value(std::make_unique<faabric::Message>(msg));
        }

        // Sync messages can't have their results read twice, so skip
        // redis
        if (!msg.isasync()) {
            return;
        }
        return;
    }

    std::string key = msg.resultkey();
    if (key.empty()) {
        throw std::runtime_error("Result key empty. Cannot publish result");
    }

    // Write the successful result to the result queue
    std::vector<uint8_t> inputData = faabric::util::messageToBytes(msg);
    redis::Redis& redis = redis::Redis::getQueue();
    redis.publishSchedulerResult(key, msg.statuskey(), inputData);
}

void Scheduler::registerThread(uint32_t msgId)
{
    // Here we need to ensure the promise is registered locally so
    // callers can start waiting
    threadResults[msgId];
}

void Scheduler::setThreadResult(const faabric::Message& msg,
                                int32_t returnValue)
{
    bool isMaster = msg.masterhost() == conf.endpointHost;

    if (isMaster) {
        setThreadResultLocally(msg.id(), returnValue);
    } else {
        SnapshotClient& c = getSnapshotClient(msg.masterhost());
        c.pushThreadResult(msg.id(), returnValue);
    }
}

void Scheduler::pushSnapshotDiffs(
  const faabric::Message& msg,
  const std::vector<faabric::util::SnapshotDiff>& diffs)
{
    bool isMaster = msg.masterhost() == conf.endpointHost;

    if (!isMaster && !diffs.empty()) {
        SnapshotClient& c = getSnapshotClient(msg.masterhost());
        c.pushSnapshotDiffs(msg.snapshotkey(), msg.groupid(), diffs);
    }
}

void Scheduler::setThreadResultLocally(uint32_t msgId, int32_t returnValue)
{
    faabric::util::SharedLock lock(mx);
    SPDLOG_DEBUG("Setting result for thread {} to {}", msgId, returnValue);
    threadResults.at(msgId).set_value(returnValue);
}

int32_t Scheduler::awaitThreadResult(uint32_t messageId)
{
    faabric::util::SharedLock lock(mx);
    auto it = threadResults.find(messageId);
    if (it == threadResults.end()) {
        SPDLOG_ERROR("Thread {} not registered on this host", messageId);
        throw std::runtime_error("Awaiting unregistered thread");
    }
    lock.unlock();
    return it->second.get_future().get();
}

faabric::Message Scheduler::getFunctionResult(unsigned int messageId,
                                              int timeoutMs,
                                              faabric::Message* caller)
{
    std::atomic_int* suspendedCtr = nullptr;
    if (caller != nullptr) {
        faabric::util::SharedLock _l(mx);
        suspendedCtr =
          &suspendedExecutors[faabric::util::funcToString(*caller, false)];
        _l.unlock();
        suspendedCtr->fetch_add(1, std::memory_order_acq_rel);
        monitorWaitingTasks.fetch_add(1, std::memory_order_acq_rel);
    }
    struct SuspendedGuard
    {
        std::atomic_int* ctr;
        ~SuspendedGuard()
        {
            if (ctr != nullptr) {
                getScheduler().monitorWaitingTasks.fetch_sub(
                  1, std::memory_order_acq_rel);
                ctr->fetch_sub(1, std::memory_order_acq_rel);
                ctr = nullptr;
            }
        }
    } suspendedGuard{ suspendedCtr };

    ZoneScopedNS("Scheduler::getFunctionResult", 5);
    ZoneValue(messageId);

    bool isBlocking = timeoutMs > 0;

    if (messageId == 0) {
        throw std::runtime_error("Must provide non-zero message ID");
    }

    do {
        std::future<std::unique_ptr<faabric::Message>> fut;
        {
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            auto it = localResults.find(messageId);
            if (it == localResults.end()) {
                break; // fallback to redis
            }
            fut = it->second->promise.get_future();
        }
        if (!isBlocking) {
            ZoneScopedNS("Wait for future", 5);
            auto status = fut.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status == std::future_status::timeout) {
                faabric::Message msgResult;
                msgResult.set_type(faabric::Message_MessageType_EMPTY);
                return msgResult;
            }
        } else {
            ZoneScopedNS("Wait for future", 5);
            fut.wait();
        }
        ZoneNamedNS(_zone_grab, "Grab future", 5, true);
        {
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            localResults.erase(messageId);
        }
        return *fut.get();
    } while (0);

    redis::Redis& redis = redis::Redis::getQueue();
    TracyMessageL("Got redis queue");

    std::string resultKey = faabric::util::resultKeyFromMessageId(messageId);

    faabric::Message msgResult;

    if (isBlocking) {
        // Blocking version will throw an exception when timing out
        // which is handled by the caller.
        std::vector<uint8_t> result = redis.dequeueBytes(resultKey, timeoutMs);
        ZoneScopedN("Parse result message");
        msgResult.ParseFromArray(result.data(), (int)result.size());
    } else {
        // Non-blocking version will tolerate empty responses, therefore
        // we handle the exception here
        std::vector<uint8_t> result;
        try {
            result = redis.dequeueBytes(resultKey, timeoutMs);
        } catch (redis::RedisNoResponseException& ex) {
            // Ok for no response when not blocking
        }

        if (result.empty()) {
            // Empty result has special type
            msgResult.set_type(faabric::Message_MessageType_EMPTY);
        } else {
            // Normal response if we get something from redis
            ZoneScopedN("Parse result message");
            msgResult.ParseFromArray(result.data(), (int)result.size());
        }
    }

    return msgResult;
}

void Scheduler::getFunctionResultAsync(
  unsigned int messageId,
  int timeoutMs,
  asio::io_context& ioc,
  asio::any_io_executor& executor,
  std::function<void(faabric::Message&)> handler)
{
    ZoneScopedNS("Scheduler::getFunctionResultAsync", 5);
    ZoneValue(messageId);

    if (messageId == 0) {
        throw std::runtime_error("Must provide non-zero message ID");
    }

    do {
        std::shared_ptr<MessageLocalResult> mlr;
        {
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            auto it = localResults.find(messageId);
            if (it == localResults.end()) {
                break; // fallback to redis
            }
            mlr = it->second;
        }
        struct MlrAwaiter : public std::enable_shared_from_this<MlrAwaiter>
        {
            unsigned int messageId;
            Scheduler* sched;
            std::shared_ptr<MessageLocalResult> mlr;
            asio::posix::stream_descriptor dsc;
            std::function<void(faabric::Message&)> handler;
            MlrAwaiter(unsigned int messageId,
                       Scheduler* sched,
                       std::shared_ptr<MessageLocalResult> mlr,
                       asio::posix::stream_descriptor dsc,
                       std::function<void(faabric::Message&)> handler)
              : messageId(messageId)
              , sched(sched)
              , mlr(std::move(mlr))
              , dsc(std::move(dsc))
              , handler(handler)
            {}
            ~MlrAwaiter() { dsc.release(); }
            void await(const boost::system::error_code& ec)
            {
                if (!ec) {
                    auto msg = mlr->promise.get_future().get();
                    handler(*msg);
                    {
                        faabric::util::UniqueLock resultsLock(
                          sched->localResultsMutex);
                        sched->localResults.erase(messageId);
                    }
                } else {
                    doAwait();
                }
            }
            void doAwait()
            {
                dsc.async_wait(asio::posix::stream_descriptor::wait_read,
                               beast::bind_front_handler(
                                 &MlrAwaiter::await, this->shared_from_this()));
            }
        };
        auto awaiter = std::make_shared<MlrAwaiter>(
          messageId,
          this,
          mlr,
          asio::posix::stream_descriptor(ioc, mlr->event_fd),
          std::move(handler));
        awaiter->doAwait();
        return;
    } while (0);

    // TODO: Non-blocking redis
    redis::Redis& redis = redis::Redis::getQueue();
    TracyMessageL("Got redis queue");

    std::string resultKey = faabric::util::resultKeyFromMessageId(messageId);

    faabric::Message msgResult;

    // Blocking version will throw an exception when timing out
    // which is handled by the caller.
    TracyMessageL("Dequeueing bytes");
    std::vector<uint8_t> result = redis.dequeueBytes(resultKey, timeoutMs);
    {
        ZoneScopedN("Parse result message");
        msgResult.ParseFromArray(result.data(), (int)result.size());
    }

    handler(msgResult);
}

faabric::HostResources Scheduler::getThisHostResources()
{
    faabric::util::SharedLock lock(mx);
    faabric::HostResources hostResources = thisHostResources;
    hostResources.set_usedslots(
      this->thisHostUsedSlots.load(std::memory_order_acquire));
    return hostResources;
}

void Scheduler::setThisHostResources(faabric::HostResources& res)
{
    faabric::util::FullLock lock(mx);
    thisHostResources = res;
    this->thisHostUsedSlots.store(res.usedslots(), std::memory_order_release);
}

faabric::HostResources Scheduler::getHostResources(const std::string& host)
{
    return getFunctionCallClient(host).getResources();
}

// --------------------------------------------
// EXECUTION GRAPH
// --------------------------------------------

#define CHAINED_SET_PREFIX "chained_"
std::string getChainedKey(unsigned int msgId)
{
    return std::string(CHAINED_SET_PREFIX) + std::to_string(msgId);
}

void Scheduler::logChainedFunction(unsigned int parentMessageId,
                                   unsigned int chainedMessageId)
{
    redis::Redis& redis = redis::Redis::getQueue();

    const std::string& key = getChainedKey(parentMessageId);
    redis.sadd(key, std::to_string(chainedMessageId));
    redis.expire(key, STATUS_KEY_EXPIRY);
}

std::set<unsigned int> Scheduler::getChainedFunctions(unsigned int msgId)
{
    redis::Redis& redis = redis::Redis::getQueue();

    const std::string& key = getChainedKey(msgId);
    const std::set<std::string> chainedCalls = redis.smembers(key);

    std::set<unsigned int> chainedIds;
    for (auto i : chainedCalls) {
        chainedIds.insert(std::stoi(i));
    }

    return chainedIds;
}

void Scheduler::updateMonitoring()
{
    if (this->monitorFd < 0) {
        return;
    }
    static std::mutex monitorMx;
    std::unique_lock<std::mutex> monitorLock(monitorMx);
    thread_local std::string wrBuffer = std::string(size_t(128), char('\0'));
    wrBuffer.clear();
    constexpr auto ord = std::memory_order_acq_rel;
    int32_t locallySched = monitorLocallyScheduledTasks.load(ord);
    int32_t started = monitorStartedTasks.load(ord);
    int32_t waiting = monitorWaitingTasks.load(ord);
    fmt::format_to(std::back_inserter(wrBuffer),
                   "local_sched,{},waiting_queued,{},started,{},"
                   "waiting,{},active,{}\n",
                   locallySched,
                   locallySched - started,
                   started,
                   waiting,
                   started - waiting);
    const size_t size = wrBuffer.size();
    flock(monitorFd, LOCK_EX);
    ftruncate(monitorFd, size);
    lseek(monitorFd, 0, SEEK_SET);
    ssize_t pos = 0;
    while (pos < size) {
        ssize_t written = write(monitorFd, wrBuffer.data() + pos, size - pos);
        if (written < 0 && errno != EAGAIN) {
            perror("Couldn't write monitoring data");
        }
        if (written == 0) {
            SPDLOG_WARN("Couldn't write monitoring data");
            break;
        }
        if (written > 0) {
            pos += written;
        }
    }
    flock(monitorFd, LOCK_UN);
}

ExecGraph Scheduler::getFunctionExecGraph(unsigned int messageId)
{
    ExecGraphNode rootNode = getFunctionExecGraphNode(messageId);
    ExecGraph graph{ .rootNode = rootNode };

    return graph;
}

ExecGraphNode Scheduler::getFunctionExecGraphNode(unsigned int messageId)
{
    redis::Redis& redis = redis::Redis::getQueue();

    // Get the result for this message
    std::string statusKey = faabric::util::statusKeyFromMessageId(messageId);

    // We want to make sure the message bytes have been populated by the
    // time we get them from Redis. For the time being, we retry a
    // number of times and fail if we don't succeed.
    std::vector<uint8_t> messageBytes = redis.get(statusKey);
    int numRetries = 0;
    while (messageBytes.empty() && numRetries < MAX_GET_EXEC_GRAPH_RETRIES) {
        SPDLOG_WARN("Retry GET message for ExecGraph node with id {} "
                    "(Retry {}/{})",
                    messageId,
                    numRetries + 1,
                    MAX_GET_EXEC_GRAPH_RETRIES);
        SLEEP_MS(GET_EXEC_GRAPH_SLEEP_MS);
        messageBytes = redis.get(statusKey);
        ++numRetries;
    }
    if (messageBytes.empty()) {
        SPDLOG_ERROR("Can't GET message from redis (id: {}, key: {})",
                     messageId,
                     statusKey);
        throw std::runtime_error("Message for exec graph not in Redis");
    }

    faabric::Message result;
    result.ParseFromArray(messageBytes.data(), (int)messageBytes.size());

    // Recurse through chained calls
    std::set<unsigned int> chainedMsgIds = getChainedFunctions(messageId);
    std::vector<ExecGraphNode> children;
    for (auto c : chainedMsgIds) {
        children.emplace_back(getFunctionExecGraphNode(c));
    }

    // Build the node
    ExecGraphNode node{ .msg = result, .children = children };

    return node;
}
}
