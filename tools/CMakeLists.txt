# ==============================================================================
#  DemonEngine — CLI Tools
# ==============================================================================

macro(demon_add_tool NAME DIR)
    set(_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/${DIR}/main.cpp")
    if(EXISTS "${_MAIN}")
        add_executable(${NAME} "${_MAIN}")
        target_link_libraries(${NAME} PRIVATE Demon::Runtime)
        target_include_directories(${NAME} PRIVATE "${CMAKE_SOURCE_DIR}/engine")
        set_target_properties(${NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
endmacro()

demon_add_tool(DemonShaderCompiler  shader_compiler)
if(TARGET DemonShaderCompiler)
    target_include_directories(DemonShaderCompiler PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/worker_common")
endif()

demon_add_tool(DemonCrashGuard crash_guard)

function(demon_add_background_worker NAME)
    add_executable(${NAME} worker_stub/main.cpp)
    target_link_libraries(${NAME} PRIVATE Demon::Runtime)
    target_include_directories(${NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/engine"
        "${CMAKE_CURRENT_SOURCE_DIR}/worker_common")
    target_compile_definitions(${NAME} PRIVATE DEMON_WORKER_NAME="${NAME}")
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()

demon_add_background_worker(DemonAssetWorker)
demon_add_background_worker(DemonStreamHost)
demon_add_background_worker(DemonThumbGen)
demon_add_background_worker(DemonCacheServer)
demon_add_background_worker(DemonScriptVM)
demon_add_background_worker(DemonPhysicsServer)
demon_add_background_worker(DemonBuildAgent)

if(DEMON_BUILD_TOOLS)
    demon_add_tool(DemonAssetImporter asset_importer)
    demon_add_tool(DemonBuilder       builder)
endif()

set(DEMON_DXC_DIR "" CACHE PATH "Directory containing the x64 DirectX Shader Compiler runtime")
set(_demon_dxc_hints)

if(DEMON_DXC_DIR)
    list(APPEND _demon_dxc_hints "${DEMON_DXC_DIR}")
endif()

if(DX12_SDK_DIR)
    file(GLOB _demon_windows_sdk_versions LIST_DIRECTORIES true "${DX12_SDK_DIR}/bin/10.*")
    list(SORT _demon_windows_sdk_versions COMPARE NATURAL ORDER DESCENDING)
    foreach(_demon_sdk_version IN LISTS _demon_windows_sdk_versions)
        list(APPEND _demon_dxc_hints "${_demon_sdk_version}/x64")
    endforeach()
    list(APPEND _demon_dxc_hints "${DX12_SDK_DIR}/bin/x64")
endif()

find_program(DEMON_DXC_EXECUTABLE
    NAMES dxc.exe dxc
    HINTS ${_demon_dxc_hints}
    DOC "Path to the x64 DirectX Shader Compiler"
)

if(DEMON_DXC_EXECUTABLE)
    get_filename_component(_demon_dxc_bin_dir "${DEMON_DXC_EXECUTABLE}" DIRECTORY)
    set(_demon_dxc_runtime_files)
    foreach(_demon_dxc_file dxc.exe dxcompiler.dll dxil.dll)
        if(EXISTS "${_demon_dxc_bin_dir}/${_demon_dxc_file}")
            list(APPEND _demon_dxc_runtime_files "${_demon_dxc_bin_dir}/${_demon_dxc_file}")
            add_custom_command(TARGET DemonShaderCompiler POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_demon_dxc_bin_dir}/${_demon_dxc_file}"
                        "$<TARGET_FILE_DIR:DemonShaderCompiler>/${_demon_dxc_file}"
            )
        endif()
    endforeach()
    install(FILES ${_demon_dxc_runtime_files} DESTINATION bin)
    message(STATUS "DemonShaderCompiler: using DXC from ${_demon_dxc_bin_dir}")
else()
    message(WARNING "DemonShaderCompiler: dxc.exe was not found. Set DEMON_DXC_DIR to a Windows SDK x64 bin directory.")
endif()
