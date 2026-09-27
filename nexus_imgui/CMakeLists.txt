cmake_minimum_required(VERSION 3.16)
project(nexus_imgui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# options
# ---------------------------------------------------------------------------
if(WIN32)
    set(NEXUS_DLL_DEFAULT ON)
else()
    set(NEXUS_DLL_DEFAULT OFF)
endif()

option(NEXUS_BUILD_DEMO  "Build the GLFW + OpenGL3 demo executable" ON)
option(NEXUS_BUILD_TOOLS "Build the headless screenshot tool and the logic unit tests" ON)
option(NEXUS_BUILD_DLL   "Build the injectable nexus.dll (WIN32/MSVC only)" ${NEXUS_DLL_DEFAULT})

# Local checkouts instead of FetchContent (useful when GitHub is unreachable).
#   NEXUS_IMGUI_DIR — a Dear ImGui v1.91.9b tree (the folder holding imgui.h).
#                     v1.92+ breaks the API used here — do NOT use it.
#   NEXUS_GLFW_DIR  — a GLFW 3.x tree (the folder holding its CMakeLists.txt).
set(NEXUS_IMGUI_DIR "" CACHE PATH "Local Dear ImGui v1.91.9b source dir (empty = FetchContent)")
set(NEXUS_GLFW_DIR  "" CACHE PATH "Local GLFW source dir (empty = FetchContent)")

# ---------------------------------------------------------------------------
# Dear ImGui (pinned tag)
# ---------------------------------------------------------------------------
if(NEXUS_IMGUI_DIR)
    if(NOT EXISTS "${NEXUS_IMGUI_DIR}/imgui.h")
        message(FATAL_ERROR
            "NEXUS_IMGUI_DIR='${NEXUS_IMGUI_DIR}' does not look like an ImGui tree (imgui.h not found)")
    endif()
    set(NEXUS_IMGUI_SRC "${NEXUS_IMGUI_DIR}")
    message(STATUS "NEXUS: using local ImGui at ${NEXUS_IMGUI_SRC}")
else()
    include(FetchContent)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.9b
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)
    set(NEXUS_IMGUI_SRC "${imgui_SOURCE_DIR}")
endif()

set(NEXUS_IMGUI_SOURCES
    ${NEXUS_IMGUI_SRC}/imgui.cpp
    ${NEXUS_IMGUI_SRC}/imgui_demo.cpp
    ${NEXUS_IMGUI_SRC}/imgui_draw.cpp
    ${NEXUS_IMGUI_SRC}/imgui_tables.cpp
    ${NEXUS_IMGUI_SRC}/imgui_widgets.cpp)

# Some ImGui bundles (e.g. the one shipped inside the `imgui-bundle` sdist)
# vendor the stack-layout extension; the upstream git tag does not. Add it only
# when it is really there, otherwise linking fails on ImGuiInternal::*.
if(EXISTS "${NEXUS_IMGUI_SRC}/imgui_stacklayout.cpp")
    list(APPEND NEXUS_IMGUI_SOURCES ${NEXUS_IMGUI_SRC}/imgui_stacklayout.cpp)
    message(STATUS "NEXUS: imgui_stacklayout.cpp found — adding it to imgui_core")
endif()

add_library(imgui_core STATIC ${NEXUS_IMGUI_SOURCES})
target_include_directories(imgui_core PUBLIC ${NEXUS_IMGUI_SRC})
if(MSVC)
    target_compile_options(imgui_core PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# NEXUS UI — the ported menu (depends on imgui core only, no GLFW/OpenGL)
# ---------------------------------------------------------------------------
add_library(nexus_ui STATIC
    src/nexus_widgets.cpp
    src/nexus_menu.cpp)
target_include_directories(nexus_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_ui PUBLIC imgui_core)
if(MSVC)
    target_compile_options(nexus_ui PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# NEXUS logic — game math, pattern scanner, ESP, vector aim.
# Platform independent: the game *backend* (Capture/SetCameraAngles/TickMisc)
# is supplied either by nexus_game_stub (headless/demo/tests) or by
# dll/game_windows.cpp (the DLL).
# ---------------------------------------------------------------------------
add_library(nexus_logic STATIC
    src/pattern.cpp
    src/game.cpp
    src/esp.cpp
    src/aim.cpp)
target_include_directories(nexus_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nexus_logic PUBLIC nexus_ui)
if(MSVC)
    target_compile_options(nexus_logic PRIVATE /utf-8)
endif()

# "there is no game here" backend
add_library(nexus_game_stub STATIC src/game_stub.cpp)
target_link_libraries(nexus_game_stub PUBLIC nexus_logic)

# ---------------------------------------------------------------------------
# Headless screenshot tool + logic unit tests (no window system needed)
# ---------------------------------------------------------------------------
if(NEXUS_BUILD_TOOLS)
    add_executable(nexus_headless tools/headless_render.cpp)
    target_link_libraries(nexus_headless PRIVATE nexus_logic nexus_game_stub)

    add_executable(nexus_logic_test tools/logic_test.cpp)
    target_link_libraries(nexus_logic_test PRIVATE nexus_logic nexus_game_stub)

    if(MSVC)
        target_compile_options(nexus_headless   PRIVATE /utf-8)
        target_compile_options(nexus_logic_test PRIVATE /utf-8)
    endif()

    enable_testing()
    add_test(NAME nexus_logic_test COMMAND nexus_logic_test)
endif()

# ---------------------------------------------------------------------------
# GLFW + OpenGL3 demo
# ---------------------------------------------------------------------------
if(NEXUS_BUILD_DEMO)
    if(NEXUS_GLFW_DIR)
        if(NOT EXISTS "${NEXUS_GLFW_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "NEXUS_GLFW_DIR='${NEXUS_GLFW_DIR}' does not contain a CMakeLists.txt")
        endif()
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
        add_subdirectory(${NEXUS_GLFW_DIR} ${CMAKE_BINARY_DIR}/glfw)
        message(STATUS "NEXUS: using local GLFW at ${NEXUS_GLFW_DIR}")
    else()
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
        include(FetchContent)
        FetchContent_Declare(glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG        3.4
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(glfw)
    endif()

    add_library(imgui_backends STATIC
        ${NEXUS_IMGUI_SRC}/backends/imgui_impl_glfw.cpp
        ${NEXUS_IMGUI_SRC}/backends/imgui_impl_opengl3.cpp)
    target_link_libraries(imgui_backends PUBLIC imgui_core glfw)
    # GLFW_INCLUDE_NONE: OpenGL entry points come from ImGui's embedded loader,
    # so no system GL headers are required to compile the backend.
    target_compile_definitions(imgui_backends PRIVATE GLFW_INCLUDE_NONE)

    add_executable(nexus_demo src/main.cpp)
    target_link_libraries(nexus_demo PRIVATE nexus_ui imgui_backends ${CMAKE_DL_LIBS})
endif()

# ---------------------------------------------------------------------------
# nexus.dll — the injectable Windows build (DX11 Present hook)
# ---------------------------------------------------------------------------
if(NEXUS_BUILD_DLL)
    if(NOT WIN32)
        message(WARNING
            "NEXUS_BUILD_DLL=ON but the host is not Windows — the 'nexus' target is skipped.")
    else()
        add_library(nexus SHARED
            dll/dllmain.cpp
            dll/hooks.cpp
            dll/game_windows.cpp
            ${NEXUS_IMGUI_SRC}/backends/imgui_impl_dx11.cpp
            ${NEXUS_IMGUI_SRC}/backends/imgui_impl_win32.cpp)
        target_include_directories(nexus PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/dll
            ${NEXUS_IMGUI_SRC}/backends)
        # d3dcompiler: ImGui's DX11 backend builds its shader with D3DCompile.
        # dwmapi:      ImGui's Win32 backend queries DWM composition.
        target_link_libraries(nexus PRIVATE
            nexus_logic d3d11 dxgi d3dcompiler dwmapi user32 gdi32 shell32)
        target_compile_definitions(nexus PRIVATE
            NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
        set_target_properties(nexus PROPERTIES OUTPUT_NAME nexus)
        if(MSVC)
            target_compile_options(nexus PRIVATE /utf-8)
        endif()
    endif()
endif()
