cmake_minimum_required(VERSION 3.24)

project(VTHOST
    VERSION 0.2.0
    DESCRIPTION "VTHOST - media server / video player para espetáculos"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(VTHOST_BUILD_ENGINE   "Compilar o motor de playback (Windows)" ON)
option(VTHOST_BUILD_WATCHDOG "Compilar o watchdog (Windows)" ON)
option(VTHOST_BUILD_STUDIO   "Compilar a interface Studio (Qt 6)" ON)
option(VTHOST_BUILD_TESTS    "Compilar os testes do núcleo" ON)

# Tudo vai parar à mesma pasta, para o Studio encontrar o watchdog e o motor.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(MSVC)
    # Avisos fortes; PDBs também em Release para analisar minidumps de crashes.
    add_compile_options(/W4 /permissive- /utf-8 /Zi /EHsc)
    add_link_options(/DEBUG /OPT:REF /OPT:ICF)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_compile_definitions(VTHOST_VERSION="${PROJECT_VERSION}")

add_subdirectory(src/core)

# Ícone (logótipo) e versão nos executáveis Windows.
if(WIN32)
    set(VTHOST_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/logo/vthost.ico")
    file(TO_CMAKE_PATH "${VTHOST_ICON_PATH}" VTHOST_ICON_PATH)
    configure_file(assets/windows/app.rc.in "${CMAKE_BINARY_DIR}/vthost_app.rc" @ONLY)
    set(VTHOST_APP_RC "${CMAKE_BINARY_DIR}/vthost_app.rc")
endif()

if(WIN32)
    add_subdirectory(src/winutil)
    if(VTHOST_BUILD_ENGINE)
        add_subdirectory(src/engine)
    endif()
    if(VTHOST_BUILD_WATCHDOG)
        add_subdirectory(src/watchdog)
    endif()
endif()

if(VTHOST_BUILD_STUDIO)
    add_subdirectory(src/studio)
endif()

if(VTHOST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
