cmake_minimum_required(VERSION 3.16)
project(nexus_imgui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NEXUS_BUILD_DEMO  "Build the GLFW + OpenGL3 demo executable" ON)
option(NEXUS_BUILD_TOOLS "Build the headless (software-rendered) screenshot tool" ON)

# ---------------------------------------------------------------------------
# Dear ImGui (pinned tag, fetched at configure time — needs network once)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui_core STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui_core PUBLIC ${imgui_SOURCE_DIR})

# ---------------------------------------------------------------------------
# NEXUS UI — the ported menu (depends on imgui core only, no GLFW/OpenGL)
# ---------------------------------------------------------------------------
add_library(nexus_ui STATIC
    src/nexus_widgets.cpp
    src/nexus_menu.cpp)
target_include_directories(nexus_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_ui PUBLIC imgui_core)

# ---------------------------------------------------------------------------
# Headless screenshot tool (works everywhere, no window system needed)
# ---------------------------------------------------------------------------
if(NEXUS_BUILD_TOOLS)
    add_executable(nexus_headless tools/headless_render.cpp)
    target_link_libraries(nexus_headless PRIVATE nexus_ui)
endif()

# ---------------------------------------------------------------------------
# GLFW + OpenGL3 demo
# ---------------------------------------------------------------------------
if(NEXUS_BUILD_DEMO)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)

    add_library(imgui_backends STATIC
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_link_libraries(imgui_backends PUBLIC imgui_core glfw)
    # GLFW_INCLUDE_NONE: OpenGL entry points come from ImGui's embedded loader,
    # so no system GL headers are required to compile the backend.
    target_compile_definitions(imgui_backends PRIVATE GLFW_INCLUDE_NONE)

    add_executable(nexus_demo src/main.cpp)
    target_link_libraries(nexus_demo PRIVATE nexus_ui imgui_backends ${CMAKE_DL_LIBS})
endif()
