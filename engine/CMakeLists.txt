# ==============================================================================
#  DemonEngine - Engine Library (DemonRuntime)
# ==============================================================================
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/input/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripting/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/serialization/*.cpp"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/renderer/backup/.*")

# In-game UI drawing (draw list, font atlas, DX12 flush) — used by the
# renderer's UI overlay pass for UIElementComponent entities.
list(APPEND ENGINE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/GUIDrawList.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/GUIFont.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/GUIRenderer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/gui/ProggyClean_ttf.cpp"
)

add_library(DemonRuntime STATIC ${ENGINE_SOURCES}
        renderer/Mesh.h
        renderer/Material.h
        renderer/stb_image.cpp)

set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/renderer/stb_image.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

target_include_directories(DemonRuntime PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include/DemonEngine>
)

target_link_libraries(DemonRuntime PUBLIC
        Demon::ThirdParty
)

if(DEMON_JOLT_AVAILABLE AND TARGET Jolt::Jolt)
    target_link_libraries(DemonRuntime PUBLIC Jolt::Jolt)
    target_compile_definitions(DemonRuntime PUBLIC DEMON_USE_JOLT)
endif()

if(WIN32)
    target_link_libraries(DemonRuntime PRIVATE
            windowscodecs
            runtimeobject
            d3d12
            dxgi
            d3dcompiler
            dxguid
    )
endif()

if(MSVC)
    target_compile_options(DemonRuntime PRIVATE
            /FS /MP /W3 /nologo
            "$<$<CONFIG:Debug>:/Od>"
            "$<$<CONFIG:Debug>:/Zi>"
            "$<$<CONFIG:Debug>:/RTC1>"
            "$<$<CONFIG:Debug>:/MDd>"
            "$<$<CONFIG:Release>:/O2>"
            "$<$<CONFIG:Release>:/MD>"
    )
endif()

if(DEMON_ASSIMP_AVAILABLE)
    if(TARGET assimp::assimp)
        target_link_libraries(DemonRuntime PUBLIC assimp::assimp)
    elseif(TARGET assimp)
        target_link_libraries(DemonRuntime PUBLIC assimp)
    endif()
    target_include_directories(DemonRuntime PUBLIC "${ASSIMP_ROOT}/include")
    target_compile_definitions(DemonRuntime PUBLIC DEMON_USE_ASSIMP)
endif()

target_precompile_headers(DemonRuntime PUBLIC
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/core/DemonPCH.h>"
)

add_library(Demon::Runtime ALIAS DemonRuntime)
