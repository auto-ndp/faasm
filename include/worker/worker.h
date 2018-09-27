#pragma once

#include <infra/infra.h>
#include <util/util.h>

#include <string>
#include <exception>
#include <tuple>
#include <thread>

#include <boost/filesystem.hpp>

#include <proto/faasm.pb.h>
#include <queue>


namespace worker {
    /** Funciton called in multithreaded execution to actually do the work */
    void execFunction(int index, message::FunctionCall call);

    /** CGroup management */
    enum CgroupMode {cg_off, cg_on};

    const std::string CG_CPU = "cpu";

    class CGroup {
    public:
        explicit CGroup();

        void addController(const std::string &controller);
        void addThread(int threadId, const std::string &controller);

    private:
        std::string name;
        CgroupMode mode;

        boost::filesystem::path getPathToController(const std::string &controller);
        boost::filesystem::path getPathToFile(const std::string &controller, const std::string &file);
        void mkdirForController(const std::string &controller);
    };

    /** Network isolation */
    enum NetworkIsolationMode {ns_off, ns_on};

    class NetworkNamespace {
    public:
        explicit NetworkNamespace(int index);
        void addCurrentThread();

    private:
        std::string name;
        NetworkIsolationMode mode;
    };

    /** Worker wrapper */
    class Worker {
    public:
        Worker();

        void start();
    private:
        std::shared_ptr<CGroup> cgroup;
    };
}