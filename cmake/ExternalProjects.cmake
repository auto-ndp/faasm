include(FindGit)
find_package(Git)
include (ExternalProject)
include (FetchContent)
find_package (Threads REQUIRED)

# Find conan-generated package descriptions
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(PREPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
  message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
  file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.16.1/conan.cmake"
                "${CMAKE_BINARY_DIR}/conan.cmake"
                EXPECTED_HASH SHA256=396e16d0f5eabdc6a14afddbcfff62a54a7ee75c6da23f32f7a31bc85db23484
                TLS_VERIFY ON)
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_check(VERSION 1.41.0 REQUIRED)

conan_cmake_configure(
    REQUIRES
        boost/1.77.0
        catch2/2.13.7
        cppcodec/0.2
        cpprestsdk/2.10.18
        cppzmq/4.8.1
        flatbuffers/2.0.0
        hiredis/1.0.2
        protobuf/3.17.1
        rapidjson/cci.20200410
        spdlog/1.9.2
        zeromq/4.3.4
    GENERATORS
        cmake_find_package
        cmake_paths
    OPTIONS
        flatbuffers:shared=True
        flatbuffers:options_from_context=False
        flatbuffers:flatc=True
        flatbuffers:flatbuffers=True
        protobuf:shared=True
        boost:shared=False
        boost:error_code_header_only=True
        boost:system_no_deprecated=True
        boost:filesystem_no_deprecated=True
        boost:zlib=False
        boost:bzip2=False
        boost:lzma=False
        boost:zstd=False
        boost:without_locale=True
        boost:without_log=True
        boost:without_mpi=True
        boost:without_python=True
        boost:without_test=True
        boost:without_wave=True
        cpprestsdk:with_websockets=False
        zeromq:encryption=None
)

conan_cmake_autodetect(FAABRIC_CONAN_SETTINGS)

conan_cmake_install(PATH_OR_REFERENCE .
                    BUILD outdated
                    REMOTE conancenter
                    PROFILE_HOST ${CMAKE_CURRENT_LIST_DIR}/../conan-profile.txt
                    PROFILE_BUILD ${CMAKE_CURRENT_LIST_DIR}/../conan-profile.txt
                    SETTINGS ${FAABRIC_CONAN_SETTINGS}
)

include(${CMAKE_CURRENT_BINARY_DIR}/conan_paths.cmake)

find_package(Boost 1.77.0 REQUIRED)
find_package(Catch2 REQUIRED)
find_package(Flatbuffers REQUIRED)
find_package(Protobuf REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(ZLIB REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(cppcodec REQUIRED)
find_package(cpprestsdk REQUIRED)
find_package(cppzmq REQUIRED)
find_package(fmt REQUIRED)
find_package(hiredis REQUIRED)
find_package(spdlog REQUIRED)

# Pistache - Conan version is out of date and doesn't support clang
FetchContent_Declare(pistache_ext
    GIT_REPOSITORY "https://github.com/pistacheio/pistache.git"
    GIT_TAG "cfd5322e80a31f00e966c23f6bbb9cae67b48bfd"
)

FetchContent_MakeAvailable(pistache_ext)
add_library(pistache::pistache ALIAS pistache_static)

# zstd (Conan version not customizable enough)
set(ZSTD_BUILD_CONTRIB OFF CACHE INTERNAL "")
set(ZSTD_BUILD_CONTRIB OFF CACHE INTERNAL "")
set(ZSTD_BUILD_PROGRAMS OFF CACHE INTERNAL "")
set(ZSTD_BUILD_SHARED OFF CACHE INTERNAL "")
set(ZSTD_BUILD_STATIC ON CACHE INTERNAL "")
set(ZSTD_BUILD_TESTS OFF CACHE INTERNAL "")
# This means zstd doesn't use threading internally,
# not that it can't be used in a multithreaded context
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE INTERNAL "")
set(ZSTD_LEGACY_SUPPORT OFF CACHE INTERNAL "")
set(ZSTD_ZLIB_SUPPORT OFF CACHE INTERNAL "")
set(ZSTD_LZMA_SUPPORT OFF CACHE INTERNAL "")
set(ZSTD_LZ4_SUPPORT OFF CACHE INTERNAL "")

FetchContent_Declare(zstd_ext
    GIT_REPOSITORY "https://github.com/facebook/zstd"
    GIT_TAG "v1.5.0"
    SOURCE_SUBDIR "build/cmake"
)

FetchContent_MakeAvailable(zstd_ext)
# Work around zstd not declaring its targets properly
target_include_directories(libzstd_static INTERFACE $<BUILD_INTERFACE:${zstd_ext_SOURCE_DIR}/lib>)
add_library(zstd::libzstd_static ALIAS libzstd_static)

# Tracy (not in Conan)
FetchContent_Declare(tracy_ext
    GIT_REPOSITORY "https://github.com/wolfpld/tracy.git"
    GIT_TAG "dfb4020a924e45e44eaaffb952afb822b2b8acea"
)
FetchContent_GetProperties(tracy_ext)
if(NOT tracy_ext_POPULATED)
  FetchContent_Populate(tracy_ext)
endif()
add_library(TracyClient STATIC ${tracy_ext_SOURCE_DIR}/TracyClient.cpp)
target_link_libraries(TracyClient PUBLIC Threads::Threads dl)
target_include_directories(TracyClient PUBLIC $<INSTALL_INTERFACE:include> $<BUILD_INTERFACE:${tracy_ext_SOURCE_DIR}>)
target_compile_features(TracyClient PUBLIC cxx_std_14)
target_compile_definitions(TracyClient PUBLIC
    # TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_BROADCAST
    TRACY_NO_FRAME_IMAGE
    TRACY_NO_VSYNC_CAPTURE
    TRACY_DELAYED_INIT
    TRACY_PORT=8086 # 8086 is the default
)
if(BUILD_SHARED_LIBS)
    target_compile_options(TracyClient PRIVATE "-fPIC")
endif()
add_library(Tracy::TracyClient ALIAS TracyClient)
export(
	TARGETS TracyClient
	FILE ${CMAKE_CURRENT_BINARY_DIR}/cmake/TracyConfig.cmake
	NAMESPACE Tracy::)
install(
    TARGETS TracyClient EXPORT TracyClientExport
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Tracy)
install(EXPORT TracyClientExport DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Tracy)

# Adds Tracy::TracyClient
message(STATUS "Tracy enabled: ${TRACY_ENABLE}")

add_library(faabric_common_dependencies INTERFACE)
target_include_directories(faabric_common_dependencies INTERFACE
    ${FAABRIC_INCLUDE_DIR}
)
target_link_libraries(faabric_common_dependencies INTERFACE
    Boost::Boost
    Boost::filesystem
    Boost::system
    cppcodec::cppcodec
    cpprestsdk::cpprestsdk
    cppzmq::cppzmq
    flatbuffers::flatbuffers
    hiredis::hiredis
    pistache::pistache
    protobuf::libprotobuf
    RapidJSON::RapidJSON
    spdlog::spdlog
    Threads::Threads
    zstd::libzstd_static
    Tracy::TracyClient
)
target_compile_definitions(faabric_common_dependencies INTERFACE
    FMT_DEPRECATED= # Suppress warnings about use of deprecated api by spdlog
)
add_library(faabric::common_dependencies ALIAS faabric_common_dependencies)
