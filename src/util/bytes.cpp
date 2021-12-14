#include <faabric/util/bytes.h>

#include <sstream>
#include <vector>

namespace faabric::util {

std::vector<uint8_t> stringToBytes(const std::string& str)
{
    if (str.empty()) {
        std::vector<uint8_t> empty;
        return empty;
    }

    // Get raw data as byte pointer
    const char* cstr = str.c_str();
    auto* rawBytes = reinterpret_cast<const uint8_t*>(cstr);

    // Wrap in bytes vector
    std::vector<uint8_t> actual(rawBytes, rawBytes + str.length());
    return actual;
}

void trimTrailingZeros(std::vector<uint8_t>& vectorIn)
{
    long i = vectorIn.size() - 1;

    while (i >= 0 && vectorIn.at((unsigned long)i) == 0) {
        i--;
    }

    if (i < 0) {
        vectorIn.clear();
    } else {
        vectorIn.resize((unsigned long)i + 1);
    }
}

int safeCopyToBuffer(const std::vector<uint8_t>& dataIn,
                     uint8_t* buffer,
                     int bufferLen)
{
    return safeCopyToBuffer(dataIn.data(), dataIn.size(), buffer, bufferLen);
}

int safeCopyToBuffer(const uint8_t* dataIn,
                     int dataLen,
                     uint8_t* buffer,
                     int bufferLen)
{
    if (dataLen == 0) {
        return 0;
    }

    if (bufferLen <= 0) {
        return dataLen;
    }

    // Truncate date being copied into a short buffer
    int copyLen = std::min(dataLen, bufferLen);
    std::copy(dataIn, dataIn + copyLen, buffer);

    return copyLen;
}

std::string bytesToString(const std::vector<uint8_t>& bytes)
{
    unsigned long byteLen = bytes.size();
    const char* charPtr = reinterpret_cast<const char*>(bytes.data());
    const std::string result = std::string(charPtr, charPtr + byteLen);

    return result;
}

std::string formatByteArrayToIntString(const std::vector<uint8_t>& bytes)
{
    std::stringstream ss;

    ss << "[";
    for (int i = 0; i < bytes.size(); i++) {
        ss << (int)bytes.at(i);

        if (i < bytes.size() - 1) {
            ss << ", ";
        }
    }
    ss << "]";

    return ss.str();
}
}
