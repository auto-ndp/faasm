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
  file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.16.1/conan.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/conan.cmake"
                EXPECTED_HASH SHA256=396e16d0f5eabdc6a14afddbcfff62a54a7ee75c6da23f32f7a31bc85db23484
                TLS_VERIFY ON)
endif()

include(${CMAKE_CURRENT_BINARY_DIR}/conan.cmake)

conan_check(VERSION 1.41.0 REQUIRED)

# conan_cmake_configure(
#     REQUIRES
#         boost/1.77.0
#     GENERATORS
#         cmake_find_package
#         cmake_paths
#     OPTIONS
#         zeromq:encryption=None
# )

# conan_cmake_autodetect(FAABRIC_CONAN_SETTINGS)

# conan_cmake_install(PATH_OR_REFERENCE .
#                     BUILD outdated
#                     REMOTE conancenter
#                     PROFILE_HOST ${CMAKE_CURRENT_LIST_DIR}/../faabric/conan-profile.txt
#                     PROFILE_BUILD ${CMAKE_CURRENT_LIST_DIR}/../faabric/conan-profile.txt
#                     SETTINGS ${FAABRIC_CONAN_SETTINGS}
# )

# include(${CMAKE_CURRENT_BINARY_DIR}/conan_paths.cmake)

# Tightly-coupled dependencies
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(wavm_ext
    GIT_REPOSITORY "https://github.com/auto-ndp/WAVM.git"
    GIT_TAG "faasm"
    CMAKE_ARGS "-DDLL_EXPORT= \
        -DDLL_IMPORT="
)

FetchContent_Declare(wamr_ext
    GIT_REPOSITORY "https://github.com/faasm/wasm-micro-runtime"
    GIT_TAG "5ac9493230902dd6ffdcbef0eeb6d5cc20fa81df"
)

set(WAVM_ENABLE_STATIC_LINKING ON CACHE INTERNAL "")
FetchContent_MakeAvailable(wavm_ext wamr_ext)

# Allow access to headers nested in other projects
FetchContent_GetProperties(wavm_ext SOURCE_DIR FAASM_WAVM_SOURCE_DIR)
message(STATUS FAASM_WAVM_SOURCE_DIR ${FAASM_WAVM_SOURCE_DIR})

FetchContent_GetProperties(wamr_ext SOURCE_DIR WAMR_ROOT_DIR)
message(STATUS WAMR_ROOT_DIR ${WAMR_ROOT_DIR})

# SGX-specific dependencies
if(FAASM_SGX_XRA)
    FetchContent_Declare(xra_ext
        GIT_REPOSITORY "https://github.com/J-Heinemann/eXtended-Remote-Attestation"
        GIT_TAG "1252f429c478d8c9052b02fc54f9f7d6ecc33594"
    )

    FetchContent_MakeAvailable(xra_ext)

    # Access to headers in XRA
    FetchContent_GetProperties(xra_ext SOURCE_DIR FAASM_XRA_ROOT_DIR)
    set(FAASM_XRA_INCLUDE_PATH ${FAASM_XRA_ROOT_DIR}/include)
endif()

# General 3rd party dependencies
ExternalProject_Add(eigen_ext
    GIT_REPOSITORY "https://gitlab.com/shillaker/eigen.git"
    GIT_TAG "faasm"
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
)

# There are some AWS docs on using the cpp sdk as an external project:
# https://github.com/aws/aws-sdk-cpp/blob/main/Docs/CMake_External_Project.md
# but they don't specify how to link the libraries, which required adding an
# extra couple of CMake targets.
set(AWS_CORE_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-core.so)
set(AWS_S3_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-s3.so)
ExternalProject_Add(aws_ext
    GIT_REPOSITORY   "https://github.com/aws/aws-sdk-cpp.git"
    GIT_TAG          "1.9.128"
    BUILD_ALWAYS     0
    TEST_COMMAND     ""
    UPDATE_COMMAND   ""
    BUILD_BYPRODUCTS ${AWS_S3_LIBRARY} ${AWS_CORE_LIBRARY}
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
    LIST_SEPARATOR    "|"
    CMAKE_ARGS       -DBUILD_SHARED_LIBS=ON
                     -DBUILD_ONLY=s3|sts
                     -DAUTORUN_UNIT_TESTS=OFF
                     -DENABLE_TESTING=OFF
                     -DCMAKE_BUILD_TYPE=Release
)

add_library(aws_ext_core SHARED IMPORTED)
add_library(aws_ext_s3 SHARED IMPORTED)
set_target_properties(aws_ext_core
    PROPERTIES IMPORTED_LOCATION
    ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-core.so)
set_target_properties(aws_ext_s3
    PROPERTIES IMPORTED_LOCATION
    ${CMAKE_INSTALL_PREFIX}/lib/libaws-cpp-sdk-s3.so)
add_dependencies(aws_ext_core aws_ext)
add_dependencies(aws_ext_s3 aws_ext)

ExternalProject_Add(catch2_ext
     GIT_REPOSITORY "https://github.com/catchorg/Catch2"
     GIT_TAG "v2.13.2"
     CMAKE_ARGS "-DCATCH_INSTALL_DOCS=OFF \
         -DCATCH_INSTALL_EXTRAS=OFF"
     CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
)
