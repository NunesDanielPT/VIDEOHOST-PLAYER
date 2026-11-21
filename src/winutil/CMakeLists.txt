# Utilitários só-Windows partilhados pelo motor e pelo watchdog.
add_library(vthost_winutil STATIC crash_handler.cpp displays.cpp)
target_include_directories(vthost_winutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vthost_winutil PUBLIC vthost_core dbghelp dxgi user32)
