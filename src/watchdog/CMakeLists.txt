add_executable(vthost-watchdog WIN32 main.cpp)
target_link_libraries(vthost-watchdog PRIVATE vthost_core vthost_winutil)

target_sources(vthost-watchdog PRIVATE ${VTHOST_APP_RC})
