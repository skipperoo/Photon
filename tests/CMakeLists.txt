find_package(Qt6 REQUIRED COMPONENTS Test Quick Concurrent Qml)

if(WIN32)
  find_package(LibRaw REQUIRED)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBRAW REQUIRED libraw)
endif()

add_executable(tst_RawEngine
    auto/tst_RawEngine.cpp
    ../src/engine/RawEngine.cpp
    ../src/engine/RawEngine.h
    ../src/engine/Denoiser.cpp
    ../src/engine/Denoiser.h
 ../src/engine/GpuSearcher.cpp
 ../src/engine/GpuSearcher.h
 ../src/engine/GpuChromaFilter.cpp
 ../src/engine/GpuChromaFilter.h
 ../src/engine/ImageDeveloper.cpp
 ../src/engine/ImageDeveloper.h
 ../src/engine/VulkanComputeContext.cpp
 ../src/engine/VulkanComputeContext.h
    ../src/managers/PreviewManager.cpp
    ../src/managers/PreviewManager.h
    ../src/managers/FileScanner.cpp
    ../src/managers/FileScanner.h
    ../src/managers/LogManager.cpp
    ../src/managers/LogManager.h
    ../src/managers/AppStateManager.cpp
    ../src/managers/AppStateManager.h
)

if(WIN32)
    target_link_libraries(tst_RawEngine
        PRIVATE Qt6::Test Qt6::Gui Qt6::Concurrent Qt6::GuiPrivate Qt6::Quick
        PRIVATE ${LibRaw_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
else()
    target_link_libraries(tst_RawEngine
        PRIVATE Qt6::Test Qt6::Gui Qt6::Concurrent Qt6::GuiPrivate Qt6::Quick
        PRIVATE ${LIBRAW_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
endif()
if(NOT MSVC)
    target_compile_options(tst_RawEngine PRIVATE -mavx2 -mfma)
else()
    target_compile_options(tst_RawEngine PRIVATE /arch:AVX2)
endif()

target_include_directories(tst_RawEngine PRIVATE
    ../src
    ../src/engine
    ../src/components
    ../src/managers
    ${CMAKE_BINARY_DIR}
    $<$<NOT:$<BOOL:${WIN32}>>:/usr/include/qt6/QtGui/6.10.1>
    $<$<NOT:$<BOOL:${WIN32}>>:/usr/include/qt6/QtGui/6.10.1/QtGui>
    $<$<NOT:$<BOOL:${WIN32}>>:/usr/include/qt6/QtCore/6.10.1>
    $<$<NOT:$<BOOL:${WIN32}>>:/usr/include/qt6/QtCore/6.10.1/QtCore>
)

if(WIN32)
    target_include_directories(tst_RawEngine PRIVATE ${LibRaw_INCLUDE_DIR})
else()
    target_include_directories(tst_RawEngine PRIVATE ${LIBRAW_INCLUDE_DIRS})
endif()

add_executable(tst_AppStateManager
    auto/tst_AppStateManager.cpp
    ../src/managers/AppStateManager.cpp
    ../src/managers/AppStateManager.h
    ../src/managers/LogManager.cpp
    ../src/managers/LogManager.h
    ../src/managers/PreviewManager.cpp
    ../src/managers/PreviewManager.h
    ../src/engine/ImageDeveloper.cpp
    ../src/engine/ImageDeveloper.h
    ../src/managers/FileScanner.cpp
    ../src/managers/FileScanner.h
 ../src/engine/Denoiser.cpp
 ../src/engine/Denoiser.h
 ../src/engine/GpuSearcher.cpp
 ../src/engine/GpuSearcher.h
 ../src/engine/GpuChromaFilter.cpp
 ../src/engine/GpuChromaFilter.h
 ../src/engine/VulkanComputeContext.cpp
 ../src/engine/VulkanComputeContext.h
)

if(WIN32)
    target_link_libraries(tst_AppStateManager
        PRIVATE Qt6::Test Qt6::Qml Qt6::Concurrent Qt6::Gui Qt6::GuiPrivate Qt6::Quick
        PRIVATE ${LibRaw_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
else()
    target_link_libraries(tst_AppStateManager
        PRIVATE Qt6::Test Qt6::Qml Qt6::Concurrent Qt6::Gui Qt6::GuiPrivate Qt6::Quick
        PRIVATE ${LIBRAW_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
endif()
if(NOT MSVC)
    target_compile_options(tst_AppStateManager PRIVATE -mavx2 -mfma)
else()
    target_compile_options(tst_AppStateManager PRIVATE /arch:AVX2)
endif()

if(WIN32)
    target_include_directories(tst_AppStateManager PRIVATE
        ../src
        ../src/managers
        ../src/engine
        ${CMAKE_BINARY_DIR}
        ${LibRaw_INCLUDE_DIR}
    )
else()
    target_include_directories(tst_AppStateManager PRIVATE
        ../src
        ../src/managers
        ../src/engine
        ${CMAKE_BINARY_DIR}
        ${LIBRAW_INCLUDE_DIRS}
    )
endif()

add_test(NAME tst_RawEngine COMMAND tst_RawEngine)
add_test(NAME tst_AppStateManager COMMAND tst_AppStateManager)
