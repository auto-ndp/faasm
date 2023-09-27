#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

/*
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/s3/S3Client.h>
*/

#include <rados/librados.h>

#include <conf/FaasmConfig.h>

#include <faabric/util/logging.h>

#define S3_REQUEST_TIMEOUT_MS 10000
#define S3_CONNECT_TIMEOUT_MS 500

namespace storage {

class file_not_found_error : std::runtime_error
{
  public:
    explicit file_not_found_error(const char* msg);
    explicit file_not_found_error(std::string msg);
};

void initFaasmS3();

void shutdownFaasmS3();

class RadosCompletion final
{
  public:
    std::shared_ptr<void> contextOwner = nullptr;
    rados_completion_t completion = nullptr;

    explicit RadosCompletion(std::shared_ptr<void> contextOwner)
      : contextOwner(std::move(contextOwner))
    {
        int err = rados_aio_create_completion(
          nullptr, nullptr, nullptr, &this->completion);
        if (err < 0) {
            SPDLOG_CRITICAL("Error creating a rados completion: {}",
                            strerror(-err));
            throw std::runtime_error("Error creating a rados completion.");
        }
    }
    RadosCompletion(const RadosCompletion&) = delete;
    RadosCompletion& operator=(const RadosCompletion&) = delete;
    RadosCompletion(RadosCompletion&& other) { *this = std::move(other); }
    RadosCompletion& operator=(RadosCompletion&& other)
    {
        this->contextOwner = std::move(other.contextOwner);
        this->completion = other.completion;
        other.completion = nullptr;
        return *this;
    }
    ~RadosCompletion()
    {
        if (completion != nullptr) {
            rados_aio_release(completion);
            completion = nullptr;
        }
    }

    void wait() const
    {
        if (completion != nullptr) {
            rados_aio_wait_for_complete(completion);
        }
    }

    bool isComplete() const
    {
        return (completion != nullptr) ? rados_aio_is_complete(completion)
                                       : true;
    }

    int getReturnValue() const
    {
        return (completion != nullptr) ? rados_aio_get_return_value(completion)
                                       : 0;
    }
};

class S3Wrapper
{
  public:
    S3Wrapper();

    void createBucket(const std::string& bucketName);

    void deleteBucket(const std::string& bucketName);

    std::vector<std::string> listBuckets();

    std::vector<std::string> listKeys(const std::string& bucketName);

    void deleteKey(const std::string& bucketName, const std::string& keyName);

    void addKeyBytes(const std::string& bucketName,
                     const std::string& keyName,
                     std::span<const uint8_t> data);

    void appendKeyBytes(const std::string& bucketName,
                        const std::string& keyName,
                        std::span<const uint8_t> data);

    void addKeyStr(const std::string& bucketName,
                   const std::string& keyName,
                   const std::string& data);

    std::vector<uint8_t> getKeyBytes(const std::string& bucketName,
                                     const std::string& keyName,
                                     bool tolerateMissing = false);

    std::string getKeyStr(const std::string& bucketName,
                          const std::string& keyName);

    // RadosCompletion asyncNdpCall(const std::string& bucketName,
    //                              const std::string& keyName,
    //                              const std::string& funcClass,
    //                              const std::string& funcName,
    //                              std::span<const uint8_t> inputData,
    //                              std::span<uint8_t> outputBuffer);

    int asyncNdpCall(const std::string& bucketName,
                                 const std::string& keyName,
                                 const std::string& funcClass,
                                 const std::string& funcName,
                                 std::span<const uint8_t> inputData,
                                 std::span<uint8_t> outputBuffer);

    /**
     * Calls setBufferLength(size) with the read size, and then writes to
     * getBuffer(). Returns number of read bytes
     */
    ssize_t getKeyPartIntoBuf(const std::string& bucketName,
                              const std::string& keyName,
                              ssize_t offset,
                              ssize_t maxLength,
                              std::function<void(ssize_t)> setBufferLength,
                              std::function<char*()> getBuffer);

    static rados_t cluster;

  private:
    const conf::FaasmConfig& faasmConf;
    /*
    Aws::Client::ClientConfiguration clientConf;
    Aws::S3::S3Client client;
    */
};
}
