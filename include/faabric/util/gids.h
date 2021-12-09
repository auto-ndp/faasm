#pragma once

#include <stdint.h>
#include <string>

typedef uint64_t faabric_gid_t;
namespace faabric::util {
faabric_gid_t generateGid();
}
