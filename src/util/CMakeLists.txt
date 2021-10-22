find_package(RapidJSON)

file(GLOB HEADERS "${FAABRIC_INCLUDE_DIR}/faabric/util/*.h")

set(LIB_FILES
        barrier.cpp
        bytes.cpp
        config.cpp
        clock.cpp
        crash.cpp
        delta.cpp
        environment.cpp
        files.cpp
        func.cpp
        gids.cpp
        json.cpp
        latch.cpp
        logging.cpp
        memory.cpp
        network.cpp
        queue.cpp
        random.cpp
        snapshot.cpp
        state.cpp
        string_tools.cpp
        timing.cpp
        testing.cpp
        ${HEADERS}
        )

faabric_lib(util "${LIB_FILES}")
