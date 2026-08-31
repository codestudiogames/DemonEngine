# ==============================================================================
#  DemonEngine — Install / Package Rules
# ==============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Install runtime library ───────────────────────────────────────────────────
if(DEMON_ASSIMP_AVAILABLE OR DEMON_JOLT_AVAILABLE)
    # Skip CMake export when Assimp/Jolt are enabled (avoids exporting their targets).
    install(TARGETS DemonRuntime
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(TARGETS DemonRuntime
        EXPORT  DemonEngineTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ── Install public headers ─────────────────────────────────────────────────────
install(DIRECTORY ${CMAKE_SOURCE_DIR}/engine/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/DemonEngine
    FILES_MATCHING PATTERN "*.h"
)

# ── Install editor executable ─────────────────────────────────────────────────
if(DEMON_BUILD_EDITOR)
    install(TARGETS DemonEditor
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ── Install tools ─────────────────────────────────────────────────────────────
if(TARGET DemonShaderCompiler)
    install(TARGETS DemonShaderCompiler
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(DEMON_BUILD_TOOLS)
    if(TARGET DemonAssetImporter)
        install(TARGETS DemonAssetImporter
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
endif()

# ── Install assets ────────────────────────────────────────────────────────────
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/DemonEngine/assets
)

# ── CMake package config ───────────────────────────────────────────────────────
if(NOT DEMON_ASSIMP_AVAILABLE AND NOT DEMON_JOLT_AVAILABLE)
    install(EXPORT DemonEngineTargets
        FILE      DemonEngineTargets.cmake
        NAMESPACE Demon::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DemonEngine
    )

    configure_package_config_file(
        ${CMAKE_SOURCE_DIR}/cmake/DemonEngineConfig.cmake.in
        ${CMAKE_BINARY_DIR}/DemonEngineConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DemonEngine
    )

    install(FILES ${CMAKE_BINARY_DIR}/DemonEngineConfig.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DemonEngine
    )
endif()

# ── CPack (installer generation) ──────────────────────────────────────────────
set(CPACK_PACKAGE_NAME        "DemonEngine")
set(CPACK_PACKAGE_VERSION     "1.0.0")
set(CPACK_PACKAGE_VENDOR      "Indie Developer")
set(CPACK_PACKAGE_DESCRIPTION "DemonEngine — Indie Game Development SDK")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://demonengine.dev")

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "DemonEngine 1.0")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "DemonEngine Team")
endif()

include(CPack)
