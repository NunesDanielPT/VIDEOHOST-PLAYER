add_executable(vthost-watchdog WIN32 main.cpp)
target_link_libraries(vthost-watchdog PRIVATE vthost_core vthost_winutil)
