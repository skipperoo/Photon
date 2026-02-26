cmake_minimum_required(VERSION 3.16)

project(Photon VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/engine/GpuDenoiser.cpp
    src/engine/GpuDenoiser.h
    src/engine/ImageDeveloper.cpp
    src/engine/ImageDeveloper.h
    src/engine/VulkanComputeContext.cpp
    src/engine/VulkanComputeContext.h
    src/engine/patch_search.comp
    src/engine/bm3d_grouping.comp
    src/engine/bm3d_transform.comp
    src/engine/bm3d_filter.comp
    src/engine/bm3d_aggregate.comp
    src/components/RawViewport.cpp
    src/components/RawViewport.h
    src/managers/AppStateManager.cpp
    src/managers/AppStateManager.h
    src/managers/ThumbnailProvider.cpp
    src/managers/ThumbnailProvider.h
    src/managers/ThumbnailImageProvider.cpp
    src/managers/ThumbnailImageProvider.h
    src/managers/FileScanner.cpp
    src/managers/FileScanner.h
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
        src/engine/bm3d_grouping.comp
        src/engine/bm3d_transform.comp
        src/engine/bm3d_filter.comp
        src/engine/bm3d_aggregate.comp
)

qt_add_qml_module(Photon
    URI "Main"
    VERSION 1.0
    RESOURCE_PREFIX "/"
    SOURCES
        src/components/RawViewport.cpp
        src/components/RawViewport.h
        src/engine/RawEngine.cpp
        src/engine/RawEngine.h
        src/engine/Denoiser.cpp
        src/engine/Denoiser.h
        src/engine/GpuSearcher.cpp
        src/engine/GpuSearcher.h
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
        content/components/Button.qml
        content/components/Switch.qml
        content/components/Input.qml
        content/components/Slider.qml
        content/components/Card.qml
        content/components/Collapsible.qml
        content/components/ControlGroup.qml
        content/components/Histogram.qml
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
        assets/icons/trash.svg
        assets/icons/download.svg
        assets/icons/folder.svg
)

set_target_properties(Photon PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(Photon
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Gui Qt6::ShaderTools Qt6::Concurrent Qt6::GuiPrivate
    PRIVATE ${LIBRAW_LIBRARIES}
    PRIVATE Vulkan::Vulkan
)
target_compile_options(Photon PRIVATE -mavx2 -mfma)
target_include_directories(Photon PRIVATE
    src
    src/engine
    src/components
    src/managers
    ${CMAKE_CURRENT_BINARY_DIR}
    /usr/include/qt6/QtGui/6.10.1
    /usr/include/qt6/QtGui/6.10.1/QtGui
    ${LIBRAW_INCLUDE_DIRS}
)

# Ensure QML types are registered before main
set_target_properties(Photon PROPERTIES
    QT_QML_IMPORT_PATH "${CMAKE_BINARY_DIR}/Main"
)

# Performance Optimizations
if(MSVC)
    target_compile_options(Photon PRIVATE /O2 /arch:AVX2 /GL)
    target_link_options(Photon PRIVATE /LTCG)
else()
    target_compile_options(Photon PRIVATE -O3 -mavx2 -mfma -flto=auto)
    target_link_options(Photon PRIVATE -flto=auto)
endif()

include(GNUInstallDirs)
install(TARGETS Photon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install QML content
install(DIRECTORY content/ DESTINATION ${CMAKE_INSTALL_BINDIR}/content)
# Install Assets
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)

if(WIN32)
    set(CPACK_GENERATOR "WIX")
    set(CPACK_WIX_UPGRADE_GUID "d2b3c4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e")
    set(CPACK_PACKAGE_VENDOR "RBLabs")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Photon")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/photon.png")
endif()

set(CPACK_PACKAGE_NAME "Photon")
set(CPACK_PACKAGE_CONTACT "support@rblabs.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance native RAW image editor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

include(CPack)

enable_testing()
add_subdirectory(tests)
