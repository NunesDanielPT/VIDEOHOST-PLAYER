# Encontra o FFmpeg (build "shared", ex. BtbN win64-lgpl-shared) em FFMPEG_ROOT.
# Cria os alvos importados FFmpeg::avformat, FFmpeg::avcodec, FFmpeg::avutil, FFmpeg::swscale, FFmpeg::swresample
# e a variável FFMPEG_DLLS (DLLs a copiar para junto dos executáveis).
#
# Uso: cmake -DFFMPEG_ROOT=third_party/ffmpeg ...   (scripts/fetch-deps.ps1 descarrega-o)

set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "Pasta do FFmpeg (include/, lib/, bin/)")

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h HINTS "${FFMPEG_ROOT}/include")

set(_ffmpeg_components avformat avcodec avutil swscale swresample)
set(FFMPEG_DLLS "")
foreach(comp IN LISTS _ffmpeg_components)
    find_library(FFMPEG_${comp}_LIBRARY NAMES ${comp} HINTS "${FFMPEG_ROOT}/lib")
    if(FFMPEG_${comp}_LIBRARY AND NOT TARGET FFmpeg::${comp})
        add_library(FFmpeg::${comp} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${comp} PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_${comp}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
    endif()
    file(GLOB _dll "${FFMPEG_ROOT}/bin/${comp}-*.dll")
    list(APPEND FFMPEG_DLLS ${_dll})
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_INCLUDE_DIR FFMPEG_avformat_LIBRARY FFMPEG_avcodec_LIBRARY
                  FFMPEG_avutil_LIBRARY FFMPEG_swscale_LIBRARY FFMPEG_swresample_LIBRARY)
