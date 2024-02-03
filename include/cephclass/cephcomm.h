#include "cephcomm_generated.h"

#include <asm-generic/errno.h>
#include <cassert>
#include <cstdio>
#include <endian.h>
#include <memory>
#include <string_view>

#include <fcntl.h>
#include <stdexcept>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <vector>

// Must compile in both C++20 and C++17 mode.

namespace faasm {

inline const std::string_view DEFAULT_FAASM_CEPH_SOCKET_PATH =
  "/run/faasm/faasm-ndp.sock";

enum class SocketType
{
    listen,
    connect
};

class CephFaasmSocket
{
  public:
    explicit CephFaasmSocket(
      SocketType type,
      std::string_view path = DEFAULT_FAASM_CEPH_SOCKET_PATH)
      : type(type)
      , bindAddr(path)
    {
        if (type == SocketType::listen) {
            ::mkdir("/run/faasm", 0777);
        } else {
            struct stat st;
            int sleepUs = 1000;
            for (int i = 0; i < 10; i++, sleepUs *= 10) {
                if (sleepUs > 2000000) {
                    sleepUs = 2000000;
                }
                int ec = ::stat(path.data(), &st);
                if (ec >= 0)
                    break;
                if (errno == ENOENT) {
                    ::usleep(sleepUs);
                    continue;
                }
                perror("Connecting to the ceph-faasm socket");
                throw std::runtime_error(
                  "Can't connect to the ceph-faasm socket");
            }
        }
        sockaddr_un addr = { AF_UNIX, { 0 } };
        if (path.size() >= sizeof(addr.sun_path)) {
            throw std::runtime_error("Path too long for socket");
        }
        size_t copied = path.copy(addr.sun_path, sizeof(addr.sun_path) - 1, 0);
        addr.sun_path[copied] = '\0';

        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            ::perror("Couldn't create an fd for a ceph-faasm socket");
            throw std::runtime_error(
              "Couldn't create an fd for a ceph-faasm socket");
        }

        linger lin;
        lin.l_onoff = 1;
        lin.l_linger = 1;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));

        switch (type) {
            case SocketType::listen: {
                unlink(addr.sun_path);
                if (::bind(fd,
                           reinterpret_cast<const sockaddr*>(&addr),
                           sizeof(addr)) < 0) {
                    ::perror("Couldn't bind the ceph-faasm socket");
                    ::close(fd);
                    fd = -1;
                    throw std::runtime_error(
                      "Couldn't bind the ceph-faasm socket");
                }
                ::chmod(addr.sun_path, 0777);
                if (::listen(fd, 1024) < 0) {
                    ::perror("Couldn't listen on the ceph-faasm socket");
                    ::close(fd);
                    fd = -1;
                    throw std::runtime_error(
                      "Couldn't listen on the ceph-faasm socket");
                }
                break;
            }
            case SocketType::connect: {
                setBlocking(false);
                if (::connect(fd,
                              reinterpret_cast<const sockaddr*>(&addr),
                              sizeof(addr)) < 0) {
                    if (errno == EINPROGRESS) {
                        if (!pollFor(POLLOUT, 5000)) {
                            ::close(fd);
                            fd = -1;
                            throw std::runtime_error(
                              "Timeout trying to connect to the ceph-faasm "
                              "socket");
                        }
                    } else {
                        int ec = errno;
                        ::perror("Couldn't connect the ceph-faasm socket");
                        ::close(fd);
                        fd = -1;
                        errno = ec;
                        throw std::runtime_error(
                          "Couldn't connect the ceph-faasm socket");
                    }
                }
                setBlocking(true);
            }
        }
    }
    CephFaasmSocket(CephFaasmSocket&) = delete;
    CephFaasmSocket& operator=(CephFaasmSocket&) = delete;
    CephFaasmSocket(CephFaasmSocket&& other)
    {
        *this = static_cast<CephFaasmSocket&&>(other);
    }
    CephFaasmSocket& operator=(CephFaasmSocket&& other)
    {
        this->type = other.type;
        this->fd = other.fd;
        other.fd = -1;
        return *this;
    }

    ~CephFaasmSocket()
    {
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
    }

    int getFd() const { return fd; }

    void setBlocking(bool blocking)
    {
        int flags = ::fcntl(fd, F_GETFL);
        if (flags < 0) {
            perror("Couldn't get ndp connection flags");
            throw std::runtime_error("Couldn't get ndp connection flags");
        }
        if (blocking) {
            flags &= ~O_NONBLOCK;
        } else {
            flags |= O_NONBLOCK;
        }
        int ec = ::fcntl(fd, F_SETFL, flags);
        if (ec < 0) {
            perror("Couldn't modify O_NONBLOCK on the ndp connection flags");
            throw std::runtime_error(
              "Couldn't modify O_NONBLOCK on the ndp connection flags");
        }
    }

    // Returns: false if timed out, true if ready
    bool pollFor(int event, int timeoutMs)
    {
        pollfd pfd = {};
        pfd.fd = fd;
        pfd.events = event;
        pfd.revents = 0;
        while (::poll(&pfd, 1, timeoutMs) < 0) {
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    continue;
                default:
                    perror("Ceph-faasm socket poll error");
                    return -errno;
            }
        }
        return pfd.revents != 0;
    }

    CephFaasmSocket accept() const
    {
        do {
            int result = ::accept(fd, nullptr, nullptr);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(
                  "Couldn't accept a cephcomm connection");
            }
            return CephFaasmSocket(result, bindAddr);
        } while (true);
    }

    void rawSendBytes(const uint8_t* data, size_t dataSize) const
    {
        assert(this->type == SocketType::connect);
        ssize_t remaining = dataSize;
        do {
            // Log the data being sent
            
            ssize_t result = ::send(fd, data, dataSize, 0);
            if (result < 0) {
                // print the error
                perror(strerror(errno));
                if (errno == EINTR) {
                    continue;
                } else if (errno == EAGAIN) {
                    continue;
                } else {
                    perror("Couldn't send cephcomm data");
                    throw std::runtime_error("Couldn't send cephcomm data, errno: " + std::to_string(errno) + " " + strerror(errno));
                }
            }
            remaining -= result;
            data += result;
            dataSize -= result;
        } while (remaining > 0);
    }

    void rawRecvBytes(uint8_t* data, size_t dataSize) const
    {
        assert(this->type == SocketType::connect);
        ssize_t remaining = dataSize;
        do {
            ssize_t result = ::recv(fd, data, dataSize, 0);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("Couldn't recv cephcomm data");
                throw std::runtime_error("Couldn't recv cephcomm data");
            }
            remaining -= result;
            data += result;
            dataSize -= result;
        } while (remaining > 0);
    }

    void sendError() const
    {
        assert(this->type == SocketType::connect);
        uint64_t netSize = ::htole64(UINT64_MAX);
        rawSendBytes(reinterpret_cast<const uint8_t*>(&netSize),
                     sizeof(netSize));
    }

    void sendMessage(const uint8_t* msgData, size_t msgSize) const
    {
        assert(this->type == SocketType::connect);
        uint64_t netSize = ::htole64(msgSize);
        rawSendBytes(reinterpret_cast<const uint8_t*>(&netSize),
                     sizeof(netSize));
        rawSendBytes(msgData, msgSize);
    }

    uint64_t recvMessageSize() const
    {
        assert(this->type == SocketType::connect);
        uint64_t netSize = 0;
        rawRecvBytes(reinterpret_cast<uint8_t*>(&netSize), sizeof(netSize));
        return ::le64toh(netSize);
    }

    void recvMessageData(uint8_t* msgData, size_t msgSize) const
    {
        assert(this->type == SocketType::connect);
        rawRecvBytes(msgData, msgSize);
    }

    std::vector<uint8_t> recvMessageVector() const
    {
        const uint64_t sz = recvMessageSize();
        if (sz == UINT64_MAX) {
            throw std::runtime_error("Error received");
        }
        std::vector<uint8_t> output((size_t(sz)));
        recvMessageData(output.data(), size_t(sz));
        return output;
    }

  private:
    // Accepted connection constructor
    CephFaasmSocket(int fd, std::string bindAddr)
      : type(SocketType::connect)
      , bindAddr(bindAddr)
      , fd(fd)
    {
    }

    SocketType type = SocketType::connect;
    std::string bindAddr;
    int fd = -1;
};

template<class FBType>
void verifyFlatbuf(const uint8_t* dataPtr, size_t dataSz)
{
    flatbuffers::Verifier verifier(dataPtr, dataSz);
    if (!verifier.VerifyBuffer<FBType>(nullptr)) {
        throw std::runtime_error("Invalid Flatbuffer encountered!");
    }
}

template<class FBType>
void verifyFlatbuf(const std::vector<uint8_t>& data)
{
    return verifyFlatbuf<FBType>(data.data(), data.size());
}

}
