include_directories(
        ${FAASM_INCLUDE_DIR}/conf
)

set(LIB_FILES
        "${FAASM_INCLUDE_DIR}/conf/FaasmConfig.h"
        FaasmConfig.cpp
        )

faasm_private_lib(conf "${LIB_FILES}")
target_link_libraries(conf PUBLIC util faabric)
