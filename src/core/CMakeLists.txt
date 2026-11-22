# Núcleo portátil: sem Windows, sem Qt. Partilhado por motor, watchdog e Studio.
add_library(vthost_core STATIC
    atomic_file.cpp
    countdown.cpp
    engine_config.cpp
    layer_props.cpp
    log.cpp
    loudness.cpp
    mapping.cpp
    osc.cpp
    paths.cpp
    project.cpp
    protocol.cpp
    show_state.cpp
    version.cpp
)
target_include_directories(vthost_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vthost_core PUBLIC nlohmann_json::nlohmann_json)
