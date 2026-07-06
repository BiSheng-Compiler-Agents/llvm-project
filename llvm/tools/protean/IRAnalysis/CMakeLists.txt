add_llvm_component_library(ProteanAnalysis
    IR2Score.cpp
    PhaseOrder.cpp
    SimulatedAnnealing.cpp

    ADDITIONAL_HEADER_DIRS
    ${LLVM_MAIN_INCLUDE_DIR}/llvm/Analysis

    DEPENDS
    intrinsics_gen
    ${MLDeps}

    LINK_LIBS
    ${MLLinkDeps}

    LINK_COMPONENTS
    TransformUtils
)
