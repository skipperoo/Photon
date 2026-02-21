cmake_minimum_required(VERSION 3.16)

project(Photon VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 ShaderTools Gui Concurrent)
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED libraw)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(Photon
    src/main.cpp
    src/engine/RawEngine.cpp
    src/engine/RawEngine.h
    src/engine/Denoiser.cpp
    src/engine/Denoiser.h
    src/engine/GpuSearcher.cpp
    src/engine/GpuSearcher.h
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
    QML_FILES
        content/views/App.qml
        content/views/WelcomeView.qml
        content/views/LibraryView.qml
        content/views/DevelopView.qml
        content/views/SettingView.qml
        content/components/Button.qml
        content/components/Input.qml
        content/components/Slider.qml
        content/components/Card.qml
        content/components/Collapsible.qml
        content/components/ControlGroup.qml
        content/components/Histogram.qml
        content/components/PresetPanel.qml
        content/components/MetadataPanel.qml
        content/theme/Theme.qml
    RESOURCES
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
)

set_target_properties(Photon PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(Photon
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Gui Qt6::ShaderTools Qt6::Concurrent
    PRIVATE ${LIBRAW_LIBRARIES}
    PRIVATE Vulkan::Vulkan
)
target_compile_options(Photon PRIVATE -mavx2 -mfma)
target_include_directories(Photon PRIVATE
    src
    src/engine
    src/components
    src/managers
    ${LIBRAW_INCLUDE_DIRS}
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

enable_testing()
add_subdirectory(tests)
