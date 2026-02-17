cmake_minimum_required(VERSION 3.16)

project(Photon VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick ShaderTools Gui Concurrent)
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED libraw)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appPhoton
    src/main.cpp
    src/engine/RawEngine.cpp
    src/engine/RawEngine.h
    src/components/RawViewport.cpp
    src/components/RawViewport.h
)

# Mark Theme.qml as a singleton (must be before qt_add_qml_module)
set_source_files_properties(content/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(appPhoton
    URI "Main"
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES
        content/views/App.qml
        content/views/LibraryView.qml
        content/views/DevelopView.qml
        content/views/SettingView.qml
        content/components/Button.qml
        content/components/Input.qml
        content/components/Slider.qml
        content/components/Card.qml
        content/theme/Theme.qml
)

set_target_properties(appPhoton PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appPhoton
    PRIVATE Qt6::Quick Qt6::Gui Qt6::ShaderTools Qt6::Concurrent
    PRIVATE ${LIBRAW_LIBRARIES}
)
target_include_directories(appPhoton PRIVATE
    src/engine
    src/components
    ${LIBRAW_INCLUDE_DIRS}
)

include(GNUInstallDirs)
install(TARGETS appPhoton
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

enable_testing()
add_subdirectory(tests)
