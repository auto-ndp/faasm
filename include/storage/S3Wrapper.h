#pragma once

#include <memory>
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
                     const std::vector<uint8_t>& data);

    void addKeyStr(const std::string& bucketName,
                   const std::string& keyName,
                   const std::string& data);

    std::vector<uint8_t> getKeyBytes(const std::string& bucketName,
                                     const std::string& keyName,
                                     bool tolerateMissing = false);

    std::string getKeyStr(const std::string& bucketName,
                          const std::string& keyName);

    void getKeyPartIntoStr(const std::string& bucketName,
                           const std::string& keyName,
                           ssize_t offset,
                           ssize_t maxLength,
                           std::string& outStr);

    static rados_t cluster;

  private:
    const conf::FaasmConfig& faasmConf;
    /*
    Aws::Client::ClientConfiguration clientConf;
    Aws::S3::S3Client client;
    */
};
}
