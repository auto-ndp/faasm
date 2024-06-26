include_directories(
        ${FAASM_INCLUDE_DIR}/faaslet
)

file(GLOB HEADERS "${FAASM_INCLUDE_DIR}/faaslet/*.h")

set(LIB_FILES
        Faaslet.cpp
        ${HEADERS}
        )

faasm_private_lib(faaslet_lib "${LIB_FILES}")

target_link_libraries(faaslet_lib PUBLIC
        faabric
        system
        threads
        storage
        )

# Include SGX lib if enabled
if(SGX_FOUND)
    target_link_libraries(faaslet_lib PUBLIC faasm_sgx)
    target_include_directories(faaslet_lib PUBLIC ${FAASM_INCLUDE_DIR}/sgx)
endif()
