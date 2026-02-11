cmake_minimum_required(VERSION 3.16)

project(vk3dgrt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_executable(${PROJECT_NAME}
    src/main.cpp
    
    # Vulkan
    src/vulkan/vkerror.h
    src/vulkan/vkcontext.h
    src/vulkan/vkcontext.cpp
    src/vulkan/vkprovider.h
    src/vulkan/vkbuffer.h
    src/vulkan/vkbuffer.cpp
    src/vulkan/vkimage.h
    src/vulkan/vkimage.cpp
    src/vulkan/vkaccelstruct.h
    src/vulkan/vkaccelstruct.cpp
    src/vulkan/vkpipeline.h
    src/vulkan/vkpipeline.cpp

    # 3DGRT
    src/3dgrt/gaussian-data.h
    src/3dgrt/gaussian-loader.h
    src/3dgrt/gaussian-loader.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${Vulkan_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan
        ${CMAKE_CURRENT_SOURCE_DIR}/src/3dgrt
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        ${Vulkan_LIBRARIES}
        glfw
        imgui
        vma
        glm
        tinyply
)