include(FindGit)
find_package(Git)
include (ExternalProject)
include (FetchContent)

include_directories(${CMAKE_INSTALL_PREFIX}/include)

# Find conan-generated package descriptions
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_BINARY_DIR})
list(PREPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_BINARY_DIR})

if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/conan.cmake")
  message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
  file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/0.18.1/conan.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/conan.cmake"
                TLS_VERIFY ON)
endif()

include(${CMAKE_CURRENT_BINARY_DIR}/conan.cmake)

conan_check(VERSION 1.51.3 REQUIRED)

# Enable revisions in the conan config
execute_process(COMMAND ${CONAN_CMD} config set general.revisions_enabled=1
                RESULT_VARIABLE RET_CODE)
if(NOT "${RET_CODE}" STREQUAL "0")
    message(FATAL_ERROR "Error setting revisions for Conan: '${RET_CODE}'")
endif()

conan_cmake_configure(
    REQUIRES
        "catch2/2.13.7@#31c8cd08e3c957a9eac8cb1377cf5863"
        "mimalloc/2.0.6@#ca9e081f2a1d78eb2b70c6d2270b56d7"
        "openssl/3.0.5@#40f4488f02b36c1193b68f585131e8ef"
        # These two dependencies are only needed to perform remote attestation
        # of SGX enclaves using Microsoft Azure's Attestation Service
        "jwt-cpp/0.6.0@#cd6b5c1318b29f4becaf807b23f7bb44"
        "picojson/cci.20210117@#2af3ad146959275c97a6957b87b9073f"
    GENERATORS
        cmake_find_package
        cmake_paths
)

conan_cmake_autodetect(FAABRIC_CONAN_SETTINGS)

conan_cmake_install(PATH_OR_REFERENCE .
                    BUILD outdated
                    UPDATE
                    REMOTE conancenter
                    PROFILE_HOST ${FAABRIC_CONAN_PROFILE}
                    PROFILE_BUILD ${FAABRIC_CONAN_PROFILE}
                    SETTINGS ${FAABRIC_CONAN_SETTINGS}
)

include(${CMAKE_CURRENT_BINARY_DIR}/conan_paths.cmake)

find_package(Catch2 REQUIRED)
find_package(mimalloc REQUIRED)
find_package(jwt-cpp REQUIRED)
find_package(picojson REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto SSL)

set(OPENSSL_CRYPTO_LIBFILE "${OPENSSL_LIBRARIES}")
list(FILTER OPENSSL_CRYPTO_LIBFILE INCLUDE REGEX "libcrypto\\.")
message(STATUS "OpenSSL Crypto lib file: ${OPENSSL_CRYPTO_LIBFILE}")

# 22/12/2021 - WARNING: we don't install AWS through Conan as the recipe proved
# very unstable and failed frequently.

# There are some AWS docs on using the cpp sdk as an external project:
# https://github.com/aws/aws-sdk-cpp/blob/main/Docs/CMake_External_Project.md
# but they don't specify how to link the libraries, which required adding an
# extra couple of CMake targets.
# There are some AWS docs on using the cpp sdk as an external project:
# https://github.com/aws/aws-sdk-cpp/blob/main/Docs/CMake_External_Project.md
# but they don't specify how to link the libraries, which required adding an
# extra couple of CMake targets.
set(AWS_CORE_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-core.a)
set(AWS_S3_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-s3.a)
ExternalProject_Add(aws_ext
    GIT_REPOSITORY   "https://github.com/aws/aws-sdk-cpp.git"
    GIT_TAG          "eb196d341ee70368d84c4231a7b2c08abf0cca00"
    BUILD_ALWAYS     0
    TEST_COMMAND     ""
    UPDATE_COMMAND   ""
    BUILD_BYPRODUCTS ${AWS_S3_LIBRARY} ${AWS_CORE_LIBRARY}
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
    LIST_SEPARATOR    "|"
    CMAKE_ARGS       -DBUILD_SHARED_LIBS=OFF
                     -DBUILD_ONLY=s3|sts
                     -DAUTORUN_UNIT_TESTS=OFF
                     -DENABLE_UNITY_BUILD=ON
                     -DENABLE_TESTING=OFF
                     -DCMAKE_BUILD_TYPE=Release
                     "-Dcrypto_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}"
                     "-Dcrypto_SHARED_LIBRARY=${OPENSSL_CRYPTO_LIBFILE}"
                     "-Dcrypto_STATIC_LIBRARY=${OPENSSL_CRYPTO_LIBFILE}"
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_library(aws_ext_core STATIC IMPORTED)
add_library(aws_ext_s3 STATIC IMPORTED)
set_target_properties(aws_ext_core
    PROPERTIES IMPORTED_LOCATION
    ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-core.a)
set_target_properties(aws_ext_s3
    PROPERTIES IMPORTED_LOCATION
    ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-s3.a)
add_dependencies(aws_ext_core aws_ext)
add_dependencies(aws_ext_s3 aws_ext)
# Merge the two libraries in one aliased interface
add_library(aws_ext_s3_lib INTERFACE)
target_link_directories(aws_ext_s3_lib INTERFACE "${CMAKE_INSTALL_PREFIX}/lib")
target_link_libraries(aws_ext_s3_lib INTERFACE aws-crt-cpp aws-c-auth aws-c-cal
    aws-c-common aws-c-compression aws-c-event-stream aws-c-http
    aws-c-io aws-c-mqtt aws-c-s3 aws-c-sdkutils aws-checksums s2n
    pthread curl OpenSSL::SSL OpenSSL::Crypto z
    aws_ext_s3 aws_ext_core
)
add_dependencies(aws_ext_s3_lib aws_ext)
add_library(AWS::s3 ALIAS aws_ext_s3_lib)

# Tightly-coupled dependencies
FetchContent_Declare(wamr_ext
    GIT_REPOSITORY "https://github.com/faasm/wasm-micro-runtime"
    GIT_TAG "78274abbaa34a9b68af7d1fbe71e7b66d9d3ca02"
)

# WAMR and WAVM both link to LLVM
# If WAVM is not linked statically like WAMR, there are some obscure
# static constructor errors in LLVM due to double-registration
set(WAVM_ENABLE_STATIC_LINKING ON CACHE INTERNAL "")

FetchContent_MakeAvailable(wamr_ext)

# Allow access to headers nested in other projects
set(FAASM_WAVM_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../third-party/wavm)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
add_subdirectory(${FAASM_WAVM_SOURCE_DIR})
message(STATUS FAASM_WAVM_SOURCE_DIR ${FAASM_WAVM_SOURCE_DIR})
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${IPO_SUPPORTED})

FetchContent_GetProperties(wamr_ext SOURCE_DIR WAMR_ROOT_DIR)
message(STATUS WAMR_ROOT_DIR ${WAMR_ROOT_DIR})
