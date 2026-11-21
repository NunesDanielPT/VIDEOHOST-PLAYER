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
)
target_link_libraries(vthost-studio PRIVATE vthost_core Qt6::Widgets Qt6::Network)

# PT e EN: o código-fonte está em inglês; a tradução portuguesa fica em pt_PT.ts.
# Regra do projecto: qualquer texto novo na interface tem de ser traduzido aqui.
qt_add_translations(vthost-studio
    TS_FILES translations/vthost_studio_pt_PT.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete)
