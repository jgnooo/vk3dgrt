cmake_minimum_required(VERSION 3.16)

project(vk3dgrt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --------------------------------------------------------------------------- #
# Vulkan
# --------------------------------------------------------------------------- #
find_package(Vulkan REQUIRED)
if (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan not found. Please install the Vulkan SDK.")
endif()
# --------------------------------------------------------------------------- #

# --------------------------------------------------------------------------- #
# GLFW
# --------------------------------------------------------------------------- #
option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)
add_subdirectory(dependencies/glfw)
# --------------------------------------------------------------------------- #

# --------------------------------------------------------------------------- #
# VMA (Vulkan Memory Allocator)
# --------------------------------------------------------------------------- #
set(VMA_DIR dependencies/vma)
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE ${VMA_DIR}/include)
# --------------------------------------------------------------------------- #

# --------------------------------------------------------------------------- #
# ImGui
# --------------------------------------------------------------------------- #
set(IMGUI_DIR dependencies/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
# --------------------------------------------------------------------------- #

# --------------------------------------------------------------------------- #
# GLM (OpenGL Mathematics)
# --------------------------------------------------------------------------- #
set(GLM_DIR dependencies/glm)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${GLM_DIR})
# --------------------------------------------------------------------------- #

# --------------------------------------------------------------------------- #
# tinyply (PLY file parser)
# --------------------------------------------------------------------------- #
set(TINYPLY_DIR dependencies/tinyply)
add_library(tinyply INTERFACE)
target_include_directories(tinyply INTERFACE ${TINYPLY_DIR}/source)
# --------------------------------------------------------------------------- #

# --------------------------------------------------------------------------- #
# tinyobjloader (OBJ file parser)
# --------------------------------------------------------------------------- #
set(TINYOBJLOADER_DIR dependencies/tinyobjloader)
add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE ${TINYOBJLOADER_DIR})
# --------------------------------------------------------------------------- #

file(GLOB PROJECT_VULKAN_SOURCES CONFIGURE_DEPENDS src/vulkan/*.h src/vulkan/*.cpp)
file(GLOB PROJECT_3DGRT_SOURCES CONFIGURE_DEPENDS src/3dgrt/*.h src/3dgrt/*.cpp)
file(GLOB PROJECT_GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.h src/gui/*.cpp)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/scene.h
    src/scene-manager.h
    src/scene-manager.cpp
    src/log.h
    
    ${PROJECT_ROOT_SOURCES}
    ${PROJECT_VULKAN_SOURCES}
    ${PROJECT_3DGRT_SOURCES}
    ${PROJECT_GUI_SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${Vulkan_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        ${Vulkan_LIBRARIES}
        glfw
        imgui
        vma
        glm
        tinyply
        tinyobjloader
)

add_definitions(
    -DDATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

# --------------------------------------------------------------------------- #
# Shader Compilation (GLSL -> SPIR-V)
#
# Compile-time specialization via -D flags (matching nvpro Slang macros):
#   RENDER_MODE          - 0=GS (default), 1=POINT, 2=SPLAT
#   COMPILE_KERNEL_DEGREE - 1=standard, 2=generalized (default)
#
# SH is always enabled (SH_ENABLED=1, MAX_SH_DEGREE=3 hardcoded in shader).
# The default compilation produces a GS-mode shader with SH enabled and
# generalized Gaussian kernel (n=2). Different configurations can be
# compiled by changing these CMake variables or adding shader variants.
# --------------------------------------------------------------------------- #
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if (NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install the Vulkan SDK.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# Create output directory
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

# List of shader files to compile
# 3DGRT shaders
set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/ray-generation.rgen
    ${SHADER_SOURCE_DIR}/any-hit.rahit
    ${SHADER_SOURCE_DIR}/closest-hit.rchit
    ${SHADER_SOURCE_DIR}/miss.rmiss
    ${SHADER_SOURCE_DIR}/mesh-closest-hit.rchit
)

# Include files (not compiled directly, but tracked for dependencies)
set(SHADER_INCLUDES
    ${SHADER_SOURCE_DIR}/utils/common.glsl
    ${SHADER_SOURCE_DIR}/utils/math.glsl
    ${SHADER_SOURCE_DIR}/utils/eval.glsl
    ${SHADER_SOURCE_DIR}/utils/spherical-harmonics.glsl
    ${SHADER_SOURCE_DIR}/utils/trace.glsl
)

# Create gaussian shader output directory
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR}/3dgrt)

# Compile each shader with compile-time defines
set(SHADER_BINARIES "")
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    # Get relative path from shader source directory
    file(RELATIVE_PATH SHADER_REL_PATH ${SHADER_SOURCE_DIR} ${SHADER_SOURCE})
    get_filename_component(SHADER_REL_DIR ${SHADER_REL_PATH} DIRECTORY)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)

    # Set output path preserving directory structure
    if(SHADER_REL_DIR)
        set(SHADER_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_REL_DIR}/${SHADER_NAME}.spv)
    else()
        set(SHADER_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    endif()

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE}
            --target-env=vulkan1.3
            -O
            -I${SHADER_SOURCE_DIR}
            -I${SHADER_SOURCE_DIR}/include
            -o ${SHADER_OUTPUT}
            ${SHADER_SOURCE}
        DEPENDS ${SHADER_SOURCE} ${SHADER_INCLUDES}
        COMMENT "Compiling shader: ${SHADER_REL_PATH}"
        VERBATIM
    )

    list(APPEND SHADER_BINARIES ${SHADER_OUTPUT})
endforeach()

# Create custom target for shaders
add_custom_target(shaders ALL DEPENDS ${SHADER_BINARIES})

# Make main target depend on shaders
add_dependencies(${PROJECT_NAME} shaders)

# Define shader directory path for C++ code
target_compile_definitions(${PROJECT_NAME} PRIVATE
    SHADER_DIR="${SHADER_BINARY_DIR}"
)