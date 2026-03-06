cmake_minimum_required(VERSION 3.23)
project(ov-prompter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Shared libraries built here land in build/lib/
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- Locate OpenVINO from GenAI runtime ---
# The GenAI package includes OpenVINO runtime, so we use it from there
set(GENAI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ext_libs/genai-src")
if(NOT EXISTS "${GENAI_ROOT}")
    message(FATAL_ERROR
        "ext_libs/genai-src is missing. Run ./download-ext-libs.sh to download prebuilt libraries.")
endif()

# Use OpenVINO from GenAI runtime
set(_OV_DIR "${GENAI_ROOT}/runtime")
if(EXISTS "${_OV_DIR}/cmake/OpenVINOConfig.cmake")
    set(OpenVINO_DIR "${_OV_DIR}/cmake")
    message(STATUS "Using OpenVINO from GenAI runtime: ${_OV_DIR}")
else()
    message(FATAL_ERROR "OpenVINO not found in GenAI runtime package")
endif()

find_package(OpenVINO REQUIRED COMPONENTS Runtime)

# --- Use prebuilt GenAI library ---
# Find the GenAI library in runtime/lib/intel64
find_library(GENAI_LIBRARY
    NAMES openvino_genai
    PATHS "${GENAI_ROOT}/runtime/lib/intel64"
    NO_DEFAULT_PATH
    REQUIRED
)

find_path(GENAI_INCLUDE_DIR
    NAMES openvino/genai/llm_pipeline.hpp
    PATHS "${GENAI_ROOT}/runtime/include"
    NO_DEFAULT_PATH
    REQUIRED
)

message(STATUS "Using prebuilt GenAI library: ${GENAI_LIBRARY}")
message(STATUS "GenAI include directory: ${GENAI_INCLUDE_DIR}")

# Create imported target for GenAI
add_library(openvino_genai SHARED IMPORTED)
set_target_properties(openvino_genai PROPERTIES
    IMPORTED_LOCATION "${GENAI_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GENAI_INCLUDE_DIR}"
)

# --- Prompter executable ---
add_executable(ov-prompter ov-prompter.cpp)
target_link_libraries(ov-prompter PRIVATE openvino_genai openvino::runtime)

# RPATH so the binary finds libs at both dev-time (build/lib) and
# install-time (../libs next to a relocated binary).
set_target_properties(ov-prompter PROPERTIES
    BUILD_RPATH "$ORIGIN/lib:$ORIGIN/../libs"
    INSTALL_RPATH "$ORIGIN/../libs"
    BUILD_RPATH_USE_ORIGIN ON
)

install(TARGETS ov-prompter DESTINATION bin)
