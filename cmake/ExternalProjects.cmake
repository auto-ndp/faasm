include(FindGit)
find_package(Git)
include (ExternalProject)
include (FetchContent)
find_package (Threads REQUIRED)

# Protobuf
set(PROTOBUF_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libprotobuf.so)
ExternalProject_Add(protobuf_ext
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG "v3.16.0"
    SOURCE_SUBDIR "cmake"
    CMAKE_CACHE_ARGS "-DCMAKE_BUILD_TYPE:STRING=Release"
        "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
        "-Dprotobuf_BUILD_TESTS:BOOL=OFF"
        "-Dprotobuf_BUILD_SHARED_LIBS:BOOL=ON"
    BUILD_BYPRODUCTS ${PROTOBUF_LIBRARY}
)
ExternalProject_Get_Property(protobuf_ext SOURCE_DIR)
set(PROTOBUF_INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/include)
set(PROTOBUF_PROTOC_EXECUTABLE ${CMAKE_INSTALL_PREFIX}/bin/protoc)
add_library(protobuf_imported SHARED IMPORTED)
add_dependencies(protobuf_imported protobuf_ext)
set_target_properties(protobuf_imported
    PROPERTIES IMPORTED_LOCATION ${PROTOBUF_LIBRARY}
)

# FlatBuffers
set(FLATBUFFERS_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libflatbuffers.so)
ExternalProject_Add(flatbuffers_ext
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG "v2.0.0"
    CMAKE_CACHE_ARGS "-DCMAKE_BUILD_TYPE:STRING=Release"
        "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
        "-DFLATBUFFERS_BUILD_SHAREDLIB:BOOL=ON"
        "-DFLATBUFFERS_BUILD_TESTS:BOOL=OFF"
    BUILD_BYPRODUCTS ${FLATBUFFERS_LIBRARY}
)
ExternalProject_Get_Property(flatbuffers_ext SOURCE_DIR)
set(FLATBUFFERS_INCLUDE_DIRS ${SOURCE_DIR}/include)
set(FLATBUFFERS_FLATC_EXECUTABLE ${CMAKE_INSTALL_PREFIX}/bin/flatc)
add_library(flatbuffers_imported SHARED IMPORTED)
add_dependencies(flatbuffers_imported flatbuffers_ext)
set_target_properties(flatbuffers_imported
    PROPERTIES IMPORTED_LOCATION ${FLATBUFFERS_LIBRARY}
)

# Pistache
set(PISTACHE_LIBNAME libpistache-0.0.002-git20210107.so)
set(PISTACHE_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/${PISTACHE_LIBNAME})
ExternalProject_Add(pistache_ext
    GIT_REPOSITORY "https://github.com/pistacheio/pistache.git"
    GIT_TAG "cfd5322e80a31f00e966c23f6bbb9cae67b48bfd"
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
        "-DBUILD_SHARED_LIBS:BOOL=ON"
    BUILD_BYPRODUCTS ${PISTACHE_LIBRARY}
)
ExternalProject_Get_Property(pistache_ext SOURCE_DIR)
set(PISTACHE_INCLUDE_DIR ${SOURCE_DIR}/include)
add_library(pistache_imported SHARED IMPORTED)
add_dependencies(pistache_imported pistache_ext)
set_target_properties(pistache_imported
    PROPERTIES IMPORTED_LOCATION ${PISTACHE_LIBRARY}
)

# RapidJSON
ExternalProject_Add(rapidjson_ext
    GIT_REPOSITORY "https://github.com/Tencent/rapidjson"
    GIT_TAG "2ce91b823c8b4504b9c40f99abf00917641cef6c"
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
        "-DRAPIDJSON_BUILD_DOC:BOOL=OFF"
        "-DRAPIDJSON_BUILD_EXAMPLES:BOOL=OFF"
        "-DRAPIDJSON_BUILD_TESTS:BOOL=OFF"
)
ExternalProject_Get_Property(rapidjson_ext SOURCE_DIR)
set(RAPIDJSON_INCLUDE_DIR ${SOURCE_DIR}/include)

# spdlog
ExternalProject_Add(spdlog_ext
    GIT_REPOSITORY "https://github.com/gabime/spdlog"
    GIT_TAG "v1.8.0"
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
)
ExternalProject_Get_Property(spdlog_ext SOURCE_DIR)
set(SPDLOG_INCLUDE_DIR ${SOURCE_DIR}/include)

# cppcodec (for base64)
ExternalProject_Add(cppcodec_ext
    GIT_REPOSITORY "https://github.com/tplgy/cppcodec"
    GIT_TAG "v0.2"
    CMAKE_ARGS "-DBUILD_TESTING=OFF"
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
)
ExternalProject_Get_Property(cppcodec_ext SOURCE_DIR)
set(CPPCODEC_INCLUDE_DIR ${SOURCE_DIR})

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
    GIT_TAG "v1.4.9"
    SOURCE_SUBDIR "build/cmake"
)

FetchContent_MakeAvailable(zstd_ext)
# Work around zstd not declaring its targets properly
target_include_directories(libzstd_static INTERFACE $<BUILD_INTERFACE:${zstd_ext_SOURCE_DIR}/lib>)
add_library(zstd::libzstd_static ALIAS libzstd_static)

# Boost libraries, the header-only ones
FetchContent_Declare(boost_ext
    URL "https://boostorg.jfrog.io/artifactory/main/release/1.77.0/source/boost_1_77_0.tar.bz2"
    URL_HASH "SHA256=fc9f85fc030e233142908241af7a846e60630aa7388de9a5fafb1f3a26840854"
)
FetchContent_GetProperties(boost_ext)
if(NOT boost_ext_POPULATED)
  FetchContent_Populate(boost_ext)
endif()
add_library(Boost INTERFACE)
target_compile_definitions(Boost INTERFACE
    BOOST_BEAST_USE_STD_STRING_VIEW
    BOOST_ASIO_NO_DEPRECATED
    BOOST_ASIO_NO_TS_EXECUTORS
    BOOST_ASIO_NO_DEFAULT_LINKED_LIBS
)
target_include_directories(Boost INTERFACE ${boost_ext_SOURCE_DIR})
target_link_libraries(Boost INTERFACE Threads::Threads)
target_compile_features(Boost INTERFACE cxx_std_17)
add_library(Boost::Boost ALIAS Boost)
# Header-only aliases
add_library(Boost::atomic ALIAS Boost)
add_library(Boost::core ALIAS Boost)
add_library(Boost::assert ALIAS Boost)
add_library(Boost::config ALIAS Boost)
add_library(Boost::container_hash ALIAS Boost)
add_library(Boost::detail ALIAS Boost)
add_library(Boost::io ALIAS Boost)
add_library(Boost::iterator ALIAS Boost)
add_library(Boost::smart_ptr ALIAS Boost)
add_library(Boost::system ALIAS Boost)
add_library(Boost::type_traits ALIAS Boost)
add_library(Boost::predef ALIAS Boost)
add_library(Boost::Boost ALIAS Boost)
add_subdirectory(${boost_ext_SOURCE_DIR}/libs/filesystem ${boost_ext_BINARY_DIR}/libs/filesystem EXCLUDE_FROM_ALL)

# Tracy
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

# ZeroMQ
set(ZEROMQ_LIBRARY ${CMAKE_INSTALL_PREFIX}/lib/libzmq.so)
ExternalProject_Add(libzeromq_ext
    GIT_REPOSITORY "https://github.com/zeromq/libzmq.git"
    GIT_TAG "v4.3.4"
    CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
        "-DCMAKE_BUILD_TESTS:BOOL=OFF"
    BUILD_BYPRODUCTS ${ZEROMQ_LIBRARY}
)
ExternalProject_Get_Property(libzeromq_ext SOURCE_DIR)
set(LIBZEROMQ_INCLUDE_DIR ${SOURCE_DIR})
ExternalProject_Add(cppzeromq_ext
    GIT_REPOSITORY "https://github.com/zeromq/cppzmq.git"
    GIT_TAG "v4.8.0"
    CMAKE_CACHE_ARGS "-DCPPZMQ_BUILD_TESTS:BOOL=OFF"
        "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
)
add_dependencies(cppzeromq_ext libzeromq_ext)
ExternalProject_Get_Property(cppzeromq_ext SOURCE_DIR)
set(CPPZEROMQ_INCLUDE_DIR ${SOURCE_DIR})
set(ZEROMQ_INCLUDE_DIR ${LIBZEROMQ_INCLUDE_DIR} ${CPPZEROMQ_INCLUDE_DIR})
add_library(zeromq_imported SHARED IMPORTED)
add_dependencies(zeromq_imported cppzeromq_ext)
set_target_properties(zeromq_imported
    PROPERTIES IMPORTED_LOCATION ${ZEROMQ_LIBRARY}
)


if(FAABRIC_BUILD_TESTS)
    # Catch (tests)
    set(CATCH_INSTALL_DOCS OFF CACHE INTERNAL "")
    set(CATCH_INSTALL_EXTRAS OFF CACHE INTERNAL "")

    ExternalProject_Add(catch_ext
        GIT_REPOSITORY "https://github.com/catchorg/Catch2"
        GIT_TAG "v2.13.2"
        CMAKE_CACHE_ARGS "-DCMAKE_INSTALL_PREFIX:STRING=${CMAKE_INSTALL_PREFIX}"
    )
    ExternalProject_Get_Property(catch_ext SOURCE_DIR)
    include_directories(${CMAKE_INSTALL_PREFIX}/include/catch2)
endif()

