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
    ../src/engine/CameraLook.cpp
    ../src/engine/CameraLook.h
    ../src/engine/EditVideo.cpp
    ../src/engine/EditVideo.h
    ../src/engine/VideoEncoder.cpp
    ../src/engine/VideoEncoder.h
    ../src/engine/CameraLookMath.h
    ../src/engine/Denoiser.cpp
    ../src/engine/Denoiser.h
    ../src/engine/AiDenoiser.cpp
    ../src/engine/AiDenoiser.h
    ../src/engine/ToneMask.cpp
    ../src/engine/ToneMask.h
 ../src/engine/GpuSearcher.cpp
 ../src/engine/GpuSearcher.h
 ../src/engine/GpuChromaFilter.cpp
 ../src/engine/GpuChromaFilter.h
 ../src/engine/ImageDeveloper.cpp
 ../src/engine/ImageDeveloper.h
 ../src/engine/ImageDecoder.cpp
 ../src/engine/ImageDecoder.h
 ../src/engine/LensFun.cpp
 ../src/engine/LensFun.h
 ../src/engine/VulkanComputeContext.cpp
 ../src/engine/VulkanComputeContext.h
    ../src/managers/PreviewManager.cpp
    ../src/managers/PreviewManager.h
    ../src/managers/CrashReporter.cpp
    ../src/managers/CrashReporter.h
    ../src/managers/ExportManager.cpp
    ../src/managers/ExportManager.h
    ../src/managers/ThumbnailProvider.cpp
    ../src/managers/ThumbnailProvider.h
    ../src/managers/EditVideoSource.cpp
    ../src/managers/EditVideoSource.h
    ../src/managers/EditVideoController.cpp
    ../src/managers/EditVideoController.h
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
        PRIVATE ${TIFF_LIBRARIES}
        PRIVATE JPEG::JPEG
        PRIVATE ${PHOTON_LCMS2_LIBRARIES}
        PRIVATE Vulkan::Vulkan
        PRIVATE Dbghelp
    )
else()
    target_link_libraries(tst_RawEngine
        PRIVATE Qt6::Test Qt6::Gui Qt6::Concurrent Qt6::GuiPrivate Qt6::Quick
        PRIVATE ${LIBRAW_LIBRARIES}
        PRIVATE ${TIFF_LIBRARIES}
        PRIVATE JPEG::JPEG
        PRIVATE ${PHOTON_LCMS2_LIBRARIES}
        PRIVATE Vulkan::Vulkan
    )
endif()
# Edit video: the pill font at the app's resource path (:/Main/assets/...).
qt_add_resources(tst_RawEngine "editvideo_fonts"
    PREFIX "/Main"
    BASE ".."
    FILES ../assets/fonts/InstrumentSans-SemiBold.ttf)

# Edit video: link FFmpeg when the app builds the video export.
if(PHOTON_ENABLE_VIDEO_EXPORT)
    target_include_directories(tst_RawEngine PRIVATE ${PHOTON_FFMPEG_INCLUDE_DIRS})
    target_link_directories(tst_RawEngine PRIVATE ${PHOTON_FFMPEG_LIBRARY_DIRS})
    target_link_libraries(tst_RawEngine PRIVATE ${PHOTON_FFMPEG_LIBRARIES})
    target_compile_definitions(tst_RawEngine PRIVATE PHOTON_HAS_VIDEO_EXPORT=1)
endif()

# AI denoise: link ncnn when the app builds it, so the engine integration
# test can run the real model (it skips without the model or sample RAW).
if(TARGET ncnn)
    target_link_libraries(tst_RawEngine PRIVATE ncnn)
    target_compile_definitions(tst_RawEngine PRIVATE PHOTON_HAS_AI_DENOISE=1)
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
    ${TIFF_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIRS}
 ${OPENCV_INCLUDE_DIRS})

if(WIN32)
    target_include_directories(tst_RawEngine PRIVATE ${LibRaw_INCLUDE_DIR})
else()
    target_include_directories(tst_RawEngine PRIVATE ${LIBRAW_INCLUDE_DIRS})
endif()

add_executable(tst_AppStateManager
    auto/tst_AppStateManager.cpp
    ../src/managers/AppStateManager.cpp
    ../src/managers/AppStateManager.h
    ../src/engine/CameraLook.cpp
    ../src/engine/CameraLook.h
    ../src/engine/CameraLookMath.h
    ../src/managers/LogManager.cpp
    ../src/managers/LogManager.h
    ../src/managers/PreviewManager.cpp
    ../src/managers/PreviewManager.h
    ../src/managers/CrashReporter.cpp
    ../src/managers/CrashReporter.h
    ../src/engine/ImageDeveloper.cpp
    ../src/engine/ImageDeveloper.h
    ../src/engine/ImageDecoder.cpp
    ../src/engine/ImageDecoder.h
    ../src/engine/LensFun.cpp
    ../src/engine/LensFun.h
    ../src/managers/FileScanner.cpp
    ../src/managers/FileScanner.h
 ../src/engine/Denoiser.cpp
 ../src/engine/Denoiser.h
 ../src/engine/AiDenoiser.cpp
 ../src/engine/AiDenoiser.h
 ../src/engine/ToneMask.cpp
 ../src/engine/ToneMask.h
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
        PRIVATE ${TIFF_LIBRARIES}
        PRIVATE JPEG::JPEG
        PRIVATE ${PHOTON_LCMS2_LIBRARIES}
        PRIVATE Vulkan::Vulkan
        PRIVATE Dbghelp
    )
else()
    target_link_libraries(tst_AppStateManager
        PRIVATE Qt6::Test Qt6::Qml Qt6::Concurrent Qt6::Gui Qt6::GuiPrivate Qt6::Quick
        PRIVATE ${LIBRAW_LIBRARIES}
        PRIVATE ${TIFF_LIBRARIES}
        PRIVATE JPEG::JPEG
        PRIVATE ${PHOTON_LCMS2_LIBRARIES}
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
        ${TIFF_INCLUDE_DIR}
        ${JPEG_INCLUDE_DIRS}
    )
endif()

add_executable(tst_ImageDecoder
    auto/tst_ImageDecoder.cpp
    ../src/engine/ImageDecoder.cpp
    ../src/engine/ImageDecoder.h
    ../src/managers/CrashReporter.cpp
    ../src/managers/CrashReporter.h
    ../src/managers/LogManager.cpp
    ../src/managers/LogManager.h
)

target_link_libraries(tst_ImageDecoder
    PRIVATE Qt6::Test Qt6::Gui
    PRIVATE ${TIFF_LIBRARIES}
    PRIVATE JPEG::JPEG
    PRIVATE ${PHOTON_LCMS2_LIBRARIES}
)

if(WIN32)
    target_link_libraries(tst_ImageDecoder PRIVATE Dbghelp)
endif()

target_include_directories(tst_ImageDecoder PRIVATE
    ../src
    ../src/engine
    ../src/managers
    ${CMAKE_BINARY_DIR}
    ${TIFF_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIRS}
    ${LCMS2_INCLUDE_DIRS}
)

add_executable(tst_PresetManager
    auto/tst_PresetManager.cpp
    ../src/managers/PresetManager.cpp
    ../src/managers/PresetManager.h
    ../src/managers/CrashReporter.cpp
    ../src/managers/CrashReporter.h
    ../src/managers/LogManager.cpp
    ../src/managers/LogManager.h
)

target_link_libraries(tst_PresetManager
    PRIVATE Qt6::Test Qt6::Gui Qt6::GuiPrivate Qt6::Qml
)

if(WIN32)
    target_link_libraries(tst_PresetManager PRIVATE Dbghelp)
endif()

target_include_directories(tst_PresetManager PRIVATE
    ../src
    ../src/managers
    ${CMAKE_BINARY_DIR}
)

add_test(NAME tst_RawEngine COMMAND tst_RawEngine)
add_test(NAME tst_PresetManager COMMAND tst_PresetManager)
add_test(NAME tst_AppStateManager COMMAND tst_AppStateManager)
add_test(NAME tst_ImageDecoder COMMAND tst_ImageDecoder)
