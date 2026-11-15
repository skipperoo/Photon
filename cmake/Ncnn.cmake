# ncnn (Tencent, BSD-3-Clause): neural-network inference for AI denoise.
#
# Always built from a pinned source release (same version on every platform),
# with Vulkan compute enabled. ncnn's in-house Vulkan loader (NCNN_SIMPLEVK)
# loads the driver at runtime, so no Vulkan SDK is needed to link it, and the
# bundled glslang compiles its compute shaders.
#
# Offline builds: pass -DFETCHCONTENT_SOURCE_DIR_NCNN=<unpacked full-source>.

include(FetchContent)

set(PHOTON_NCNN_VERSION "20261020")
set(PHOTON_NCNN_URL
    "https://github.com/Tencent/ncnn/releases/download/${PHOTON_NCNN_VERSION}/ncnn-${PHOTON_NCNN_VERSION}-full-source.zip")
set(PHOTON_NCNN_SHA256
    "699d9acc67489eee0993a551ca1ebc247bbf0f4e33babe1dec810e6a9a52b988")

# Library-only build: no tools, examples, benchmark, tests, bindings or install.
set(NCNN_VULKAN ON CACHE BOOL "" FORCE)
set(NCNN_SIMPLEVK ON CACHE BOOL "" FORCE)
set(NCNN_SYSTEM_GLSLANG OFF CACHE BOOL "" FORCE)
set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NCNN_C_API OFF CACHE BOOL "" FORCE)
set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
set(NCNN_PIXEL_DRAWING OFF CACHE BOOL "" FORCE)
# Photon uses C++ exceptions/RTTI itself; keep ncnn's defaults compatible.
set(NCNN_DISABLE_RTTI OFF CACHE BOOL "" FORCE)
set(NCNN_DISABLE_EXCEPTION OFF CACHE BOOL "" FORCE)

set(_ncnn_fetch_extra "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    # Option unknown to older CMake (the Ubuntu 22.04 image ships 3.22).
    set(_ncnn_fetch_extra DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
FetchContent_Declare(
    ncnn
    URL "${PHOTON_NCNN_URL}"
    URL_HASH "SHA256=${PHOTON_NCNN_SHA256}"
    ${_ncnn_fetch_extra}
)
FetchContent_MakeAvailable(ncnn)

# Inference speed must not depend on Photon's build type: an -O0 (Debug) ncnn
# is many times slower, so always optimise it (the last -O flag wins).
if(NOT MSVC)
    target_compile_options(ncnn PRIVATE -O2)
endif()

message(STATUS "[ Ncnn.cmake ] - ncnn ${PHOTON_NCNN_VERSION} (Vulkan) enabled for AI denoise")
