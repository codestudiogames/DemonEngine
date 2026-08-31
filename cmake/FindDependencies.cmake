# ==============================================================================
#  DemonEngine - cmake/FindDependencies.cmake
#  Resolves every third-party library from the paths supplied in CMakePresets.json.
#  Nothing here needs editing; change paths in CMakePresets.json only.
# ==============================================================================

# -- Helper: fatal error with a friendly install hint -------------------------
function(demon_require_path VAR FILE HINT)
    if(NOT EXISTS "${${VAR}}/${FILE}")
        message(FATAL_ERROR
            "\n[DemonEngine] X  ${VAR} is wrong or not set.\n"
            "  Looked for : ${${VAR}}/${FILE}\n"
            "  Current    : ${${VAR}}\n"
            "  How to fix : ${HINT}\n"
            "  Then edit  : CMakePresets.json -> cacheVariables -> ${VAR}\n")
    endif()
endfunction()

# ==============================================================================
# 1. DirectX 12 SDK (Windows 10 SDK) + DirectX-Headers
# ==============================================================================
if(NOT DEFINED DX12_SDK_DIR)
    set(DX12_SDK_DIR "C:/Program Files (x86)/Windows Kits/10")
endif()
if(NOT DEFINED DX_HEADERS_DIR)
    set(DX_HEADERS_DIR "C:/DirectX-Headers")
endif()

demon_require_path(DX12_SDK_DIR "Include"
    "Install the Windows 10 SDK and set DX12_SDK_DIR to its root.\n"
    "  Default expected location: C:/Program Files (x86)/Windows Kits/10")
demon_require_path(DX_HEADERS_DIR "include/directx/d3dx12.h"
    "Install DirectX-Headers and set DX_HEADERS_DIR to its root.\n"
    "  Default expected location: C:/DirectX-Headers")

file(GLOB DX12_INCLUDE_VERSIONS "${DX12_SDK_DIR}/Include/10.*")
if(NOT DX12_INCLUDE_VERSIONS)
    message(FATAL_ERROR
        "\n[DemonEngine] X  No Windows 10 SDK version folders found under:\n"
        "  ${DX12_SDK_DIR}/Include\n")
endif()
list(SORT DX12_INCLUDE_VERSIONS)
list(GET DX12_INCLUDE_VERSIONS -1 DX12_INCLUDE_DIR)
get_filename_component(DX12_SDK_VERSION "${DX12_INCLUDE_DIR}" NAME)

set(DX12_UM_DIR     "${DX12_SDK_DIR}/Include/${DX12_SDK_VERSION}/um")
set(DX12_SHARED_DIR "${DX12_SDK_DIR}/Include/${DX12_SDK_VERSION}/shared")
set(DX12_LIB_DIR    "${DX12_SDK_DIR}/Lib/${DX12_SDK_VERSION}/um/x64")

demon_require_path(DX12_SDK_DIR "Include/${DX12_SDK_VERSION}/um/d3d12.h"
    "Verify the Windows 10 SDK installation. Missing d3d12.h.")
demon_require_path(DX12_SDK_DIR "Include/${DX12_SDK_VERSION}/shared/dxgi1_6.h"
    "Verify the Windows 10 SDK installation. Missing dxgi1_6.h.")
demon_require_path(DX12_SDK_DIR "Lib/${DX12_SDK_VERSION}/um/x64/d3d12.lib"
    "Verify the Windows 10 SDK installation. Missing d3d12.lib.")

add_library(directx12 INTERFACE IMPORTED GLOBAL)
if(MINGW)
    # MinGW must use the Windows headers and import libraries shipped with its
    # own toolchain. Mixing Microsoft SDK headers with MinGW CRT headers causes
    # incompatible integer suffixes, NULL definitions, and COM declarations.
    target_include_directories(directx12 INTERFACE "${DX_HEADERS_DIR}/include")
else()
    target_include_directories(directx12 INTERFACE
        "${DX12_UM_DIR}"
        "${DX12_SHARED_DIR}"
        "${DX_HEADERS_DIR}/include"
    )
    target_link_directories(directx12 INTERFACE "${DX12_LIB_DIR}")
endif()
target_link_libraries(directx12 INTERFACE d3d12 dxgi dxguid d3dcompiler)
message(STATUS "[Deps] DirectX12  ${DX12_SDK_VERSION}  ->  ${DX12_UM_DIR}")

# Win32 system libs
add_library(win32 INTERFACE IMPORTED GLOBAL)
target_link_libraries(win32 INTERFACE user32 gdi32 shell32 ole32 imm32)
message(STATUS "[Deps] Win32      user32;gdi32;shell32;ole32;imm32")

# ==============================================================================
# 2. GLFW removed (Win32 native windowing).

# 3. GLM (header-only)
# ==============================================================================
if(NOT DEFINED GLM_DIR)
    set(GLM_DIR "C:/glm-master/glm")
endif()

if(EXISTS "${GLM_DIR}/glm.hpp")
    get_filename_component(GLM_INCLUDE_ROOT "${GLM_DIR}" DIRECTORY)
elseif(EXISTS "${GLM_DIR}/glm/glm.hpp")
    set(GLM_INCLUDE_ROOT "${GLM_DIR}")
else()
    message(FATAL_ERROR
        "\n[DemonEngine] X  GLM not found.\n"
        "  Tried: ${GLM_DIR}/glm.hpp\n"
        "         ${GLM_DIR}/glm/glm.hpp\n"
        "  GLM_DIR should point to the folder CONTAINING glm.hpp.\n"
        "  e.g.  git clone https://github.com/g-truc/glm C:/glm-master\n"
        "        then set GLM_DIR = C:/glm-master/glm\n")
endif()

add_library(glm INTERFACE IMPORTED GLOBAL)
set_target_properties(glm PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_ROOT}"
    INTERFACE_COMPILE_DEFINITIONS
        "GLM_FORCE_RADIANS;GLM_FORCE_DEPTH_ZERO_TO_ONE;GLM_ENABLE_EXPERIMENTAL"
)
message(STATUS "[Deps] GLM        ${GLM_INCLUDE_ROOT}")

# ==============================================================================
# 4. Convenience alias
#    Anything that links Demon::ThirdParty gets ALL deps automatically.
# ==============================================================================
add_library(Demon::ThirdParty INTERFACE IMPORTED GLOBAL)
target_link_libraries(Demon::ThirdParty INTERFACE
    win32
    glm
    directx12
)
