add_executable(vthost_core_tests core_tests.cpp)
target_link_libraries(vthost_core_tests PRIVATE vthost_core)
add_test(NAME core COMMAND vthost_core_tests)
