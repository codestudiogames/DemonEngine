# ==============================================================================
#  DemonEngine v1.2 — Root CMakeLists.txt
#  Target IDE  : CLion 25  (uses bundled CMake + Ninja — NO manual MinGW needed)
#  Compiler    : MSVC (Visual Studio 2022 Build Tools)
#  Standard    : C++20
#  Graphics    : DirectX 12 (Windows 10 SDK)
#  Deps        : GLM · SDL3 · Assimp — pre-installed, path-based
#
#  ── HOW TO OPEN ────────────────────────────────────────────────────────────────
#  1. File → Open  → select this folder
#  2. CLion auto-detects CMakePresets.json and shows Debug / Release profiles
#  3. Hit ▶ Run  — CLion calls Ninja automatically; no terminal needed
#
#  ── FIRST-TIME PATH SETUP ──────────────────────────────────────────────────────
#  Edit CMakePresets.json (sits next to this file) — change cacheVariables
#  to match where you installed GLM / SDL3.
#  DirectX 12 is found via DX12_SDK_DIR (Windows 10 SDK) + DX_HEADERS_DIR.
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

project(DemonEngine
        VERSION     1.2.0
        DESCRIPTION "DemonEngine — Game Development SDK"
        LANGUAGES   CXX C
)

# ── C++20 ──────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Output directories ─────────────────────────────────────────────────────────
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ── Build options ──────────────────────────────────────────────────────────────
option(DEMON_BUILD_EDITOR       "Build the DemonEngine Editor"    ON)
option(DEMON_BUILD_TOOLS        "Build CLI tools"                 ON)
option(DEMON_BUILD_TEMPLATES    "Build project templates"         ON)
option(DEMON_ENABLE_DX12_DEBUG  "Enable D3D12 debug layer"        ON)
option(DEMON_USE_ASSIMP         "Enable Assimp for model import"  ON)
option(DEMON_USE_JOLT           "Enable Jolt Physics integration" ON)

set(ASSIMP_ROOT "E:/assimp-master/assimp-master" CACHE PATH "Path to Assimp source root")
set(JOLT_ROOT "C:/JoltPhysics" CACHE PATH "Path to JoltPhysics source root")
if(NOT EXISTS "${JOLT_ROOT}/Jolt/Jolt.cmake" AND DEFINED JOLTPHYSICS_DIR)
    get_filename_component(_DEMON_JOLT_FROM_PRESET "${JOLTPHYSICS_DIR}/.." ABSOLUTE)
    if(EXISTS "${_DEMON_JOLT_FROM_PRESET}/Jolt/Jolt.cmake")
        set(JOLT_ROOT "${_DEMON_JOLT_FROM_PRESET}" CACHE PATH "Path to JoltPhysics source root" FORCE)
    endif()
endif()

# ── Global compile definitions ─────────────────────────────────────────────────
add_compile_definitions(
        DEMON_PLATFORM_WINDOWS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
        $<$<BOOL:${DEMON_ENABLE_DX12_DEBUG}>:DEMON_DX12_DEBUG>
        $<$<CONFIG:Debug>:DEMON_DEBUG>
        $<$<CONFIG:Release>:DEMON_RELEASE>
)

# ── Compiler flags (GCC / MinGW) ───────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
            -Wall -Wextra
            -Wno-unused-parameter
            -Wno-missing-field-initializers
            -Wno-cast-function-type
    )
    add_compile_options("$<$<CONFIG:Debug>:-O0;-g3>")
    add_compile_options("$<$<CONFIG:Release>:-O3>")
    if(MINGW)
        link_libraries(gdi32 user32 shell32 ole32 uuid dwmapi)
    endif()
endif()

# ── Compiler flags (MSVC) ──────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/FS /MP /W3 /nologo /EHsc)
    add_compile_options("$<$<CONFIG:Debug>:/Od>")
    add_compile_options("$<$<CONFIG:Debug>:/Zi>")
    add_compile_options("$<$<CONFIG:Debug>:/RTC1>")
    add_compile_options("$<$<CONFIG:Debug>:/MDd>")
    add_compile_options("$<$<CONFIG:Release>:/O2>")
    add_compile_options("$<$<CONFIG:Release>:/MD>")
    add_compile_options("$<$<CONFIG:Release>:/GL>")
    add_link_options("$<$<CONFIG:Release>:/LTCG>")
    link_libraries(
            runtimeobject
            kernel32 user32 gdi32 winspool
            shell32 ole32 oleaut32 uuid
            comdlg32 advapi32
            d3d12 dxgi d3dcompiler dxguid
    )
endif()

# ==============================================================================
#  Third-party dependency resolution
#  All paths come from CMakePresets.json cacheVariables — edit that file.
# ==============================================================================
include(cmake/FindDependencies.cmake)

# ==============================================================================
#  Assimp (optional, for FBX/GLTF/etc)
# ==============================================================================
if(DEMON_USE_ASSIMP AND EXISTS "${ASSIMP_ROOT}/CMakeLists.txt")
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_SAMPLES      OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL            OFF CACHE BOOL "" FORCE)
    add_subdirectory("${ASSIMP_ROOT}" "${CMAKE_BINARY_DIR}/assimp_build" EXCLUDE_FROM_ALL)
    set(DEMON_ASSIMP_AVAILABLE TRUE)
endif()

if(DEMON_USE_JOLT AND EXISTS "${JOLT_ROOT}/Jolt/Jolt.cmake")
    set(PHYSICS_REPO_ROOT "${JOLT_ROOT}")
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    include("${JOLT_ROOT}/Jolt/Jolt.cmake")
    set(CMAKE_CXX_STANDARD          20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS        OFF)
    set(DEMON_JOLT_AVAILABLE TRUE)
endif()

# ==============================================================================
#  Subprojects
# ==============================================================================
add_subdirectory(engine)

if(DEMON_BUILD_TOOLS OR DEMON_BUILD_EDITOR)
    add_subdirectory(tools)
endif()
if(DEMON_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
if(DEMON_BUILD_TEMPLATES)
    add_subdirectory(templates)
endif()

include(cmake/Install.cmake)

# ── Build summary ──────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  ██████╗ ███████╗███╗   ███╗ ██████╗ ███╗   ██╗")
message(STATUS "  ██╔══██╗██╔════╝████╗ ████║██╔═══██╗████╗  ██║")
message(STATUS "  ██║  ██║█████╗  ██╔████╔██║██║   ██║██╔██╗ ██║")
message(STATUS "  ██║  ██║██╔══╝  ██║╚██╔╝██║██║   ██║██║╚██╗██║")
message(STATUS "  ██████╔╝███████╗██║ ╚═╝ ██║╚██████╔╝██║ ╚████║")
message(STATUS "  ╚═════╝ ╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═══╝")
message(STATUS "")
message(STATUS "  Game Development Kit - By CODE STUDIO GAMES. ")
message(STATUS "  Engine for the BLACK DEMON Series. ")
message(STATUS "  Version   : ${PROJECT_VERSION}")
message(STATUS "  Config    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler  : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Generator : ${CMAKE_GENERATOR}")
message(STATUS "  Output    : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
