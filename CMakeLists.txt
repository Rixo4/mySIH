cmake_minimum_required(VERSION 3.24)
project(SiliconPatientPlatform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_ARCHITECTURES native CACHE STRING "CUDA architectures" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SPP_SOURCES
    engine/main.cpp
    engine/neuron/NeuronModel.cpp
    engine/neuron/ReceptorModel.cpp
    engine/drug/DrugModel.cpp
    engine/synapse/Synapse.cpp
    engine/synapse/NeuromodulatorSystem.cpp
    engine/synapse/ReuptakeTransporter.cpp
    engine/synapse/NeurotransmitterPool.cpp
    engine/network/Network.cpp
    engine/simulation/SimulationEngine.cpp
    engine/simulation/BatchedSimulationEngine.cpp
    engine/analyzer/Metrics.cpp
    engine/analyzer/NetworkAnalyzer.cpp
    engine/analyzer/PharmaDecisionEngine.cpp
    engine/output/CsvWriter.cpp
    engine/cuda/CudaSimulator.cpp
)

set(SPP_ENABLE_CUDA OFF)
set(SPP_CAN_ENABLE_CUDA ON)

if(WIN32)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        message(STATUS "Visual Studio generator detected; enabling CUDA host compiler discovery via MSVC toolchain.")
    else()
        find_program(SPP_MSVC_CL cl.exe)
        if(NOT SPP_MSVC_CL)
            set(SPP_CAN_ENABLE_CUDA OFF)
            message(STATUS "cl.exe not found in PATH; CUDA build disabled on Windows host.")
        endif()
    endif()
endif()

if(SPP_CAN_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)

    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND)
            set(SPP_ENABLE_CUDA ON)
            list(APPEND SPP_SOURCES engine/cuda/NeuronUpdate.cu)
            message(STATUS "CUDA enabled with compiler: ${CMAKE_CUDA_COMPILER}")
        else()
            message(STATUS "CUDA compiler detected but CUDAToolkit not found; building CPU-only.")
        endif()
    else()
        message(STATUS "CUDA compiler not available; building CPU-only.")
    endif()
endif()

add_executable(silicon_patient ${SPP_SOURCES}
    engine/synapse/NeurotransmitterPool.cpp)

target_include_directories(silicon_patient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

if(SPP_ENABLE_CUDA)
    target_compile_definitions(silicon_patient PRIVATE SPP_USE_CUDA)
    set_target_properties(silicon_patient PROPERTIES
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON
    )
    target_link_libraries(silicon_patient PRIVATE CUDA::cudart CUDA::curand)
endif()

if(MSVC)
    target_compile_options(silicon_patient PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /O2>
    )
else()
    target_compile_options(silicon_patient PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -O3>
    )
endif()