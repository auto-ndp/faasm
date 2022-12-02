#include "cephcomm_generated.h"

#include <cassert>
#include <cstdio>
#include <endian.h>
#include <memory>
#include <string_view>

#include <stdexcept>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <vector>

// Must compile in both C++20 and C++17 mode.

namespace faasm {

inline const std::string_view DEFAULT_FAASM_CEPH_SOCKET_PATH =
  "/run/faasm-ndp.sock";

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
        switch (type) {
            case SocketType::listen: {
                if (::bind(fd,
                           reinterpret_cast<const sockaddr*>(&addr),
                           sizeof(addr)) < 0) {
                    ::perror("Couldn't bind the ceph-faasm socket");
                    ::close(fd);
                    fd = -1;
                    throw std::runtime_error(
                      "Couldn't bind the ceph-faasm socket");
                }
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
                if (::connect(fd,
                              reinterpret_cast<const sockaddr*>(&addr),
                              sizeof(addr)) < 0) {
                    ::perror("Couldn't connect the ceph-faasm socket");
                    ::close(fd);
                    fd = -1;
                    throw std::runtime_error(
                      "Couldn't connect the ceph-faasm socket");
                }
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

    struct pollfd getAcceptPollFd() const
    {
        pollfd pfd = {};
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        return pfd;
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
            ssize_t result = ::send(fd, data, dataSize, 0);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("Couldn't send cephcomm data");
                throw std::runtime_error("Couldn't send cephcomm data");
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
        rawSendBytes(msgData, msgSize);
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
