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
