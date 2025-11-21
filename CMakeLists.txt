# CMakeLists.txt for FourierForge

cmake_minimum_required(VERSION 3.16)

# Suppress FetchContent_Populate warnings
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

project(FourierForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(SDL2 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)

# --- ImGui ---
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG docking GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

# --- ImGuiFileDialog ---
FetchContent_Declare(igfd GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_GetProperties(igfd)
if(NOT igfd_POPULATED)
  FetchContent_Populate(igfd)
endif()

# Sources
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${igfd_SOURCE_DIR}/ImGuiFileDialog.cpp 
)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(FourierForge ${SOURCES} ${IMGUI_SOURCES})

target_include_directories(FourierForge PRIVATE 
    include 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
    ${igfd_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
)

target_link_libraries(FourierForge PRIVATE
    ${SDL2_LIBRARIES}
    ${GLEW_LIBRARIES}
    OpenGL::GL
    glm::glm
)

if(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
    file(COPY "${CMAKE_SOURCE_DIR}/shaders" DESTINATION "${CMAKE_BINARY_DIR}")
endif()