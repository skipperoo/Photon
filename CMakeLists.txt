cmake_minimum_required(VERSION 3.16)

project(Photon VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set MSVC runtime to dynamic to match vcpkg libraries
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Configure version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Version.h"
)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 ShaderTools Gui Concurrent GuiPrivate)
find_package(Vulkan REQUIRED)

if(WIN32)
    # Use find_package for vcpkg compatibility on Windows
    find_package(LibRaw REQUIRED)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    add_compile_definitions(NOMINMAX)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBRAW REQUIRED libraw)
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(Photon
    src/main.cpp
    src/engine/RawEngine.cpp
    src/engine/RawEngine.h
    src/engine/Denoiser.cpp
    src/engine/Denoiser.h
    src/engine/GpuSearcher.cpp
    src/engine/GpuSearcher.h
    src/engine/GpuChromaFilter.cpp
    src/engine/GpuChromaFilter.h
    src/engine/ImageDeveloper.cpp
    src/engine/ImageDeveloper.h
    src/engine/VulkanComputeContext.cpp
    src/engine/VulkanComputeContext.h
    src/engine/patch_search.comp
    src/components/RawViewport.cpp
    src/components/RawViewport.h
    src/components/ToneLutProvider.h
    src/managers/AppStateManager.cpp
    src/managers/AppStateManager.h
    src/managers/ThumbnailProvider.cpp
    src/managers/ThumbnailProvider.h
    src/managers/ThumbnailImageProvider.cpp
    src/managers/ThumbnailImageProvider.h
    src/managers/FileScanner.cpp
    src/managers/FileScanner.h
    src/managers/KeyTracker.h
    src/managers/PresetManager.cpp
    src/managers/PresetManager.h
    src/managers/LogManager.cpp
    src/managers/LogManager.h
    src/managers/ExportManager.cpp
    src/managers/ExportManager.h
    src/managers/PreviewManager.cpp
    src/managers/PreviewManager.h
)

# Mark Theme.qml as a singleton (must be before qt_add_qml_module)
set_source_files_properties(content/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt6_add_shaders(Photon "raw_shaders"
    PREFIX "/Main/shaders"
    BASE "src/components"
    FILES
        src/components/RawViewport.vert
        src/components/RawViewport.frag
        src/components/PatchSearch.vert
        src/components/PatchSearch.frag
        src/components/ColorMask.frag
)

qt6_add_shaders(Photon "compute_shaders"
    PREFIX "/Main/shaders"
    BASE "src/engine"
    BATCHABLE compute_shaders
    OUTPUT_TYPE spirv
    GLSL "450"
    ESSL "0"
    FILES
        src/engine/patch_search.comp
        src/engine/box_filter.comp
        src/engine/guided_ops.comp
)

qt_add_qml_module(Photon
    URI "Main"
    VERSION 1.0
    RESOURCE_PREFIX "/"
    SOURCES
        src/components/RawViewport.cpp
        src/components/RawViewport.h
        src/components/ToneLutProvider.h
        src/engine/RawEngine.cpp
        src/engine/RawEngine.h
        src/engine/Denoiser.cpp
        src/engine/Denoiser.h
        src/engine/GpuSearcher.cpp
        src/engine/GpuSearcher.h
        src/engine/GpuChromaFilter.cpp
        src/engine/GpuChromaFilter.h
        src/engine/ImageDeveloper.cpp
        src/engine/ImageDeveloper.h
        src/engine/VulkanComputeContext.cpp
        src/engine/VulkanComputeContext.h
        src/managers/AppStateManager.cpp
        src/managers/AppStateManager.h
        src/managers/ThumbnailProvider.cpp
        src/managers/ThumbnailProvider.h
        src/managers/ThumbnailImageProvider.cpp
        src/managers/ThumbnailImageProvider.h
        src/managers/FileScanner.cpp
        src/managers/FileScanner.h
        src/managers/KeyTracker.h
        src/managers/PresetManager.cpp
        src/managers/PresetManager.h
        src/managers/LogManager.cpp
        src/managers/LogManager.h
        src/managers/ExportManager.cpp
        src/managers/ExportManager.h
        src/managers/PreviewManager.cpp
        src/managers/PreviewManager.h
    QML_FILES
        content/views/App.qml
        content/views/WelcomeView.qml
        content/views/LibraryView.qml
        content/views/DevelopView.qml
        content/views/SettingView.qml
        content/components/PhotonButton.qml
        content/components/PhotonSwitch.qml
        content/components/PhotonCheckbox.qml
        content/components/PhotonInput.qml
        content/components/PhotonSlider.qml
        content/components/PhotonScrollBar.qml
        content/components/Card.qml
        content/components/Collapsible.qml
        content/components/ControlGroup.qml
        content/components/Histogram.qml
        content/components/ToneCurve.qml
        content/components/PresetPanel.qml
        content/components/MetadataPanel.qml
        content/components/ExportPanel.qml
        content/theme/Theme.qml
    RESOURCES
        assets/icons/photon.png
        assets/icons/search.svg
        assets/icons/hand.svg
        assets/icons/zoom-in.svg
        assets/icons/zoom-out.svg
        assets/icons/undo.svg
        assets/icons/redo.svg
        assets/icons/rotate-ccw.svg
        assets/icons/info.svg
        assets/icons/settings.svg
        assets/icons/crop.svg
        assets/icons/telescope.svg
        assets/icons/bookmark.svg
        assets/icons/library.svg
        assets/icons/gear.svg
        assets/icons/tube.svg
        assets/icons/trash.svg
        assets/icons/download.svg
        assets/icons/folder.svg
        assets/icons/home.svg
        assets/icons/eye.svg
)

set_target_properties(Photon PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(WIN32)
    target_link_libraries(Photon
        PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Gui Qt6::ShaderTools Qt6::Concurrent Qt6::GuiPrivate
        PRIVATE ${LibRaw_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
else()
    target_link_libraries(Photon
        PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Gui Qt6::ShaderTools Qt6::Concurrent Qt6::GuiPrivate
        PRIVATE ${LIBRAW_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
endif()

# Performance Optimizations
if(MSVC)
    target_compile_options(Photon PRIVATE /O2 /arch:AVX2 /GL)
    target_link_options(Photon PRIVATE /LTCG)
else()
    target_compile_options(Photon PRIVATE -O3 -mavx2 -mfma -flto=auto)
    target_link_options(Photon PRIVATE -flto=auto)
endif()
target_include_directories(Photon PRIVATE
    src
    src/engine
    src/components
    src/managers
    ${CMAKE_CURRENT_BINARY_DIR}
    $<$<NOT:$<BOOL:${WIN32}>>:/usr/include/qt6/QtGui/6.10.1>
    $<$<NOT:$<BOOL:${WIN32}>>:/usr/include/qt6/QtGui/6.10.1/QtGui>
)

# Ensure QML types are registered before main
set_target_properties(Photon PROPERTIES
    QT_QML_IMPORT_PATH "${CMAKE_BINARY_DIR}/Main"
)


include(GNUInstallDirs)
install(TARGETS Photon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET Photon
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(SCRIPT ${deploy_script})

qt_generate_deploy_qml_app_script(
    TARGET Photon
    OUTPUT_SCRIPT deploy_script
)
install(SCRIPT ${deploy_script})
# Install QML content
install(DIRECTORY content/ DESTINATION ${CMAKE_INSTALL_BINDIR}/content)
# Install Assets
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)

# Install vcpkg DLLs on Windows
if(WIN32)
  # Find LibRaw DLL based on build configuration
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(GLOB LIBRAW_DLL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/rawd.dll")
  else()
    file(GLOB LIBRAW_DLL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/raw.dll")
  endif()
  if(LIBRAW_DLL)
    install(FILES ${LIBRAW_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
  
  # Find and install all vcpkg DLLs (but not raw.dll again)
  file(GLOB VCPKG_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
  if(VCPKG_DLLS)
    list(REMOVE_ITEM VCPKG_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/raw.dll")
    install(FILES ${VCPKG_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

if(WIN32)
  set(CPACK_GENERATOR "WIX")
  set(CPACK_WIX_UPGRADE_GUID "d2b3c4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e")
  set(CPACK_PACKAGE_VENDOR "RBLabs")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "Photon")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
  set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/photon.png")
  set(CPACK_PACKAGING_INSTALL_PREFIX "")
endif()

set(CPACK_PACKAGE_NAME "Photon")
set(CPACK_PACKAGE_CONTACT "support@rblabs.net")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance native RAW image editor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

include(CPack)

enable_testing()
add_subdirectory(tests)
