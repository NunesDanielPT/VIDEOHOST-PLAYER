find_package(Qt6 6.5 QUIET COMPONENTS Widgets Network LinguistTools)
if(NOT Qt6_FOUND)
    message(WARNING "Qt 6 não encontrado: o VTHOST Studio não vai ser compilado. "
                    "Defina CMAKE_PREFIX_PATH para a instalação do Qt (ex. C:/Qt/6.8.3/msvc2022_64).")
    return()
endif()

qt_standard_project_setup()

qt_add_executable(vthost-studio WIN32
    main.cpp
    main_window.cpp
    main_window.h
    engine_client.cpp
    engine_client.h
    settings_dialog.cpp
    settings_dialog.h
    update_checker.cpp
    update_checker.h
    app_info.h
    prefs.h
    dashboard_table.cpp
    dashboard_table.h
    layer_header.cpp
    layer_header.h
    outputs_dialog.cpp
    outputs_dialog.h
    library_panel.cpp
    library_panel.h
    media_cache.cpp
    media_cache.h
    preview_widget.cpp
    preview_widget.h
    properties_panel.cpp
    properties_panel.h
    timeline_panel.cpp
    timeline_panel.h
    timeline_widget.cpp
    timeline_widget.h
)
target_link_libraries(vthost-studio PRIVATE vthost_core Qt6::Widgets Qt6::Network)
if(WIN32)
    target_sources(vthost-studio PRIVATE ${VTHOST_APP_RC})
    # Mesma deteção de ecrãs que o motor (ids iguais: "\\.\DISPLAY2").
    target_link_libraries(vthost-studio PRIVATE vthost_winutil)
endif()

# Logótipo (ícone da janela e Definições → Sobre).
qt_add_resources(vthost-studio "branding"
    PREFIX "/branding"
    BASE "${CMAKE_SOURCE_DIR}/assets/logo"
    FILES "${CMAKE_SOURCE_DIR}/assets/logo/vthost-icon-256.png" "${CMAKE_SOURCE_DIR}/assets/logo/vthost-logo.png")

# Miniaturas e duração das media: o Studio usa as mesmas DLLs do FFmpeg que o motor.
find_package(FFmpeg QUIET)
if(FFmpeg_FOUND)
    target_link_libraries(vthost-studio PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale)
    target_compile_definitions(vthost-studio PRIVATE VTHOST_HAVE_FFMPEG)
endif()

# PT e EN: o código-fonte está em inglês; a tradução portuguesa fica em pt_PT.ts.
# Regra do projecto: qualquer texto novo na interface tem de ser traduzido aqui.
qt_add_translations(vthost-studio
    TS_FILES translations/vthost_studio_pt_PT.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete)
