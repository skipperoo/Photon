# AI denoise models: RawNIND UtNet2, GPL-3.0 weights by Benoit Brummer
# (UCLouvain), as published by darktable-ai. Two variants from the same
# archive: Bayer (2x2 RGGB mosaic -> RGB, joint demosaic) and Linear
# (demosaicked linear Rec.2020 RGB, for X-Trans, Foveon, linear DNGs and any
# other non-Bayer RAW). Weights are NOT stored in git: at configure time this
# module downloads the pinned darktable-ai archive and the pinned pnnx
# converter, verifies both by SHA-256, converts each ONNX model to ncnn
# (.param/.bin) and stages them in ${CMAKE_BINARY_DIR}/models, which is
# installed next to the executable (bin/models) for the AppImage / installer.
#
# Options:
#   PHOTON_BUNDLE_AI_MODEL=ON      download + convert (default)
#   PHOTON_AI_MODEL_SOURCE_DIR=... use already converted
#                                  rawnind_{bayer,linear}.ncnn.{param,bin} from
#                                  a folder (offline builds)

option(PHOTON_BUNDLE_AI_MODEL "Download, convert and bundle the RawNIND model" ON)
if(PHOTON_BUNDLE_AI_MODEL AND CMAKE_VERSION VERSION_LESS "3.19")
  message(WARNING "[ AiDenoiseModel.cmake ] - CMake >= 3.19 is needed to fetch the "
                  "AI denoise model; building without it (AI denoise falls back to Classic).")
  set(PHOTON_BUNDLE_AI_MODEL OFF)
endif()
set(PHOTON_AI_MODEL_SOURCE_DIR "" CACHE PATH
    "Folder with prebuilt rawnind_{bayer,linear}.ncnn.param/.bin (skips download)")

set(PHOTON_AI_MODEL_DIR "${CMAKE_BINARY_DIR}/models")
set(_rawnind_variants bayer linear)

# Pinned inputs.
set(_dtmodel_url
    "https://github.com/darktable-org/darktable-ai/releases/download/release-5.6.0/rawdenoise-nind.dtmodel")
set(_dtmodel_sha256 "d71b5f1e727c85a359e6f74dca9e2016c9d8fc3e2f7ac3e9b347d80ceca969af")
set(_pnnx_version "20261020")
# Per variant: ONNX file in the archive, its hash, the pnnx input shape and the
# expected converter output (pnnx is deterministic for these models).
set(_bayer_onnx_sha256 "da27509dab6a2915da67e988acd86cf71f9d5bbc8d1aa0ed32933578a887b901")
set(_bayer_inputshape "[1,4,512,512]")
set(_bayer_param_sha256 "daf2541eb2b323599e737f314740c614a23368a33a74dfcddf746bbce7350c16")
set(_bayer_bin_sha256 "63e5d061741bdb7e70c7b1e7d74544a7b7be29d9f8ee7851ef5e95cacb63f47f")
set(_linear_onnx_sha256 "df957efadcc152c007d5d3b0917bdff9e41c0d4a0efe56584ef30b36393cd181")
set(_linear_inputshape "[1,3,512,512]")
set(_linear_param_sha256 "9686a7ce2aa95978f8f50ca2d64bff45b9285793db71fcf80f5da830132aa137")
set(_linear_bin_sha256 "ebbbedbdb1382bae855b36dc4f42a731e551936743b224b2ddc4167629c5417b")

function(_photon_check_model_hashes variant)
  file(SHA256 "${PHOTON_AI_MODEL_DIR}/rawnind_${variant}.ncnn.param" _p)
  file(SHA256 "${PHOTON_AI_MODEL_DIR}/rawnind_${variant}.ncnn.bin" _b)
  if(NOT _p STREQUAL _${variant}_param_sha256 OR NOT _b STREQUAL _${variant}_bin_sha256)
    message(WARNING "[ AiDenoiseModel.cmake ] - converted ${variant} model hashes differ "
                    "from the reference conversion (param ${_p}, bin ${_b}); the model "
                    "will still be bundled — verify the output if this is unexpected.")
  endif()
endfunction()

function(_photon_models_staged out)
  set(_all TRUE)
  foreach(v IN LISTS _rawnind_variants)
    if(NOT EXISTS "${PHOTON_AI_MODEL_DIR}/rawnind_${v}.ncnn.param" OR
       NOT EXISTS "${PHOTON_AI_MODEL_DIR}/rawnind_${v}.ncnn.bin")
      set(_all FALSE)
    endif()
  endforeach()
  set(${out} ${_all} PARENT_SCOPE)
endfunction()

function(_photon_stage_model_notice)
  configure_file("${CMAKE_SOURCE_DIR}/cmake/rawnind/NOTICE.md"
                 "${PHOTON_AI_MODEL_DIR}/RAWNIND-NOTICE.md" COPYONLY)
  configure_file("${CMAKE_SOURCE_DIR}/cmake/rawnind/LICENSE-GPL-3.0.txt"
                 "${PHOTON_AI_MODEL_DIR}/RAWNIND-LICENSE-GPL-3.0.txt" COPYONLY)
endfunction()

if(PHOTON_AI_MODEL_SOURCE_DIR)
  file(MAKE_DIRECTORY "${PHOTON_AI_MODEL_DIR}")
  foreach(v IN LISTS _rawnind_variants)
    if(EXISTS "${PHOTON_AI_MODEL_SOURCE_DIR}/rawnind_${v}.ncnn.param" AND
       EXISTS "${PHOTON_AI_MODEL_SOURCE_DIR}/rawnind_${v}.ncnn.bin")
      configure_file("${PHOTON_AI_MODEL_SOURCE_DIR}/rawnind_${v}.ncnn.param"
                     "${PHOTON_AI_MODEL_DIR}/rawnind_${v}.ncnn.param" COPYONLY)
      configure_file("${PHOTON_AI_MODEL_SOURCE_DIR}/rawnind_${v}.ncnn.bin"
                     "${PHOTON_AI_MODEL_DIR}/rawnind_${v}.ncnn.bin" COPYONLY)
      _photon_check_model_hashes(${v})
    else()
      message(WARNING "[ AiDenoiseModel.cmake ] - rawnind_${v}.ncnn.{param,bin} missing in "
                      "${PHOTON_AI_MODEL_SOURCE_DIR}; photos needing the ${v} model use Classic")
    endif()
  endforeach()
  _photon_stage_model_notice()
  message(STATUS "[ AiDenoiseModel.cmake ] - using prebuilt models from ${PHOTON_AI_MODEL_SOURCE_DIR}")
elseif(PHOTON_BUNDLE_AI_MODEL)
  _photon_models_staged(_staged)
  if(_staged)
    message(STATUS "[ AiDenoiseModel.cmake ] - RawNIND models already staged in ${PHOTON_AI_MODEL_DIR}")
  else()
    # Host converter build.
    if(CMAKE_HOST_WIN32)
      set(_pnnx_pkg "pnnx-${_pnnx_version}-windows")
      set(_pnnx_sha256 "e4f91348a399a0060a6e7dd323d40427e1cbdcab67469cf1904b5d09b23606c5")
      set(_pnnx_exe "pnnx.exe")
    elseif(CMAKE_HOST_APPLE)
      set(_pnnx_pkg "pnnx-${_pnnx_version}-macos")
      set(_pnnx_sha256 "03ed124a4d9351c3bcadbd7bdef1f59851746bfcf80bd87b01aa766abb2766f7")
      set(_pnnx_exe "pnnx")
    else()
      set(_pnnx_pkg "pnnx-${_pnnx_version}-linux")
      set(_pnnx_sha256 "6cfff631178e02e62542e72bf14e0045dfce62095bbc1f633176e9c0778ab8a6")
      set(_pnnx_exe "pnnx")
    endif()

    set(_work "${CMAKE_BINARY_DIR}/_ai_model_work")
    file(MAKE_DIRECTORY "${_work}" "${PHOTON_AI_MODEL_DIR}")

    message(STATUS "[ AiDenoiseModel.cmake ] - downloading RawNIND model archive")
    file(DOWNLOAD "${_dtmodel_url}" "${_work}/rawdenoise-nind.dtmodel"
         EXPECTED_HASH "SHA256=${_dtmodel_sha256}" STATUS _st TLS_VERIFY ON)
    list(GET _st 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "[ AiDenoiseModel.cmake ] - model download failed: ${_st}. "
                          "Use -DPHOTON_BUNDLE_AI_MODEL=OFF or PHOTON_AI_MODEL_SOURCE_DIR.")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_work}/rawdenoise-nind.dtmodel"
         DESTINATION "${_work}/dtmodel" PATTERNS "*.onnx")

    message(STATUS "[ AiDenoiseModel.cmake ] - downloading pnnx converter ${_pnnx_pkg}")
    file(DOWNLOAD "https://github.com/pnnx/pnnx/releases/download/${_pnnx_version}/${_pnnx_pkg}.zip"
         "${_work}/${_pnnx_pkg}.zip"
         EXPECTED_HASH "SHA256=${_pnnx_sha256}" STATUS _st TLS_VERIFY ON)
    list(GET _st 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "[ AiDenoiseModel.cmake ] - pnnx download failed: ${_st}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_work}/${_pnnx_pkg}.zip" DESTINATION "${_work}")
    set(_pnnx "${_work}/${_pnnx_pkg}/${_pnnx_exe}")
    if(NOT CMAKE_HOST_WIN32)
      file(CHMOD "${_pnnx}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endif()

    # pnnx writes its outputs next to the input model.
    foreach(v IN LISTS _rawnind_variants)
      file(GLOB_RECURSE _onnx "${_work}/dtmodel/*model_${v}.onnx")
      if(NOT _onnx)
        message(FATAL_ERROR "[ AiDenoiseModel.cmake ] - model_${v}.onnx not found in archive")
      endif()
      list(GET _onnx 0 _onnx)
      file(SHA256 "${_onnx}" _onnx_hash)
      if(NOT _onnx_hash STREQUAL _${v}_onnx_sha256)
        message(FATAL_ERROR "[ AiDenoiseModel.cmake ] - model_${v}.onnx hash mismatch (${_onnx_hash})")
      endif()
      set(_conv "${_work}/convert_${v}")
      file(MAKE_DIRECTORY "${_conv}")
      configure_file("${_onnx}" "${_conv}/rawnind_${v}.onnx" COPYONLY)
      message(STATUS "[ AiDenoiseModel.cmake ] - converting RawNIND ${v} ONNX -> ncnn")
      execute_process(
          COMMAND "${_pnnx}" "rawnind_${v}.onnx" "inputshape=${_${v}_inputshape}"
          WORKING_DIRECTORY "${_conv}"
          RESULT_VARIABLE _rv OUTPUT_QUIET ERROR_VARIABLE _err)
      if(NOT _rv EQUAL 0 OR NOT EXISTS "${_conv}/rawnind_${v}.ncnn.param")
        message(FATAL_ERROR "[ AiDenoiseModel.cmake ] - pnnx conversion of ${v} failed (${_rv}): ${_err}")
      endif()
      configure_file("${_conv}/rawnind_${v}.ncnn.param"
                     "${PHOTON_AI_MODEL_DIR}/rawnind_${v}.ncnn.param" COPYONLY)
      configure_file("${_conv}/rawnind_${v}.ncnn.bin"
                     "${PHOTON_AI_MODEL_DIR}/rawnind_${v}.ncnn.bin" COPYONLY)
      _photon_check_model_hashes(${v})
    endforeach()
    file(REMOVE_RECURSE "${_work}")
  endif()
  _photon_stage_model_notice()
else()
  message(STATUS "[ AiDenoiseModel.cmake ] - model not bundled (PHOTON_BUNDLE_AI_MODEL=OFF); "
                 "AI denoise looks for models in <app>/models or $PHOTON_AI_MODEL_DIR")
endif()

if(EXISTS "${PHOTON_AI_MODEL_DIR}/rawnind_bayer.ncnn.param" OR
   EXISTS "${PHOTON_AI_MODEL_DIR}/rawnind_linear.ncnn.param")
  install(DIRECTORY "${PHOTON_AI_MODEL_DIR}/" DESTINATION ${CMAKE_INSTALL_BINDIR}/models)
endif()
