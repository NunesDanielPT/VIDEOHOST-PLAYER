find_package(FFmpeg REQUIRED)

add_executable(vthost-engine WIN32
    main.cpp
    engine_app.cpp
    renderer.cpp
    output_window.cpp
    video_decoder.cpp
    layer_player.cpp
    ipc_server.cpp
    ndi_output.cpp
)
target_link_libraries(vthost-engine PRIVATE
    vthost_core
    vthost_winutil
    FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale
    d3d11 dxgi d3dcompiler winmm)

# Copia as DLLs do FFmpeg para junto do executável.
foreach(dll IN LISTS FFMPEG_DLLS)
    add_custom_command(TARGET vthost-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:vthost-engine>")
endforeach()
# d3dcompiler_47.dll faz parte do Windows 10/11; não é preciso distribuir.

target_sources(vthost-engine PRIVATE ${VTHOST_APP_RC})
