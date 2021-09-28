#pragma once

#include <string>
#include <stdint.h>

typedef uint64_t faabric_gid_t;
namespace faabric::util {
faabric_gid_t generateGid();
}
