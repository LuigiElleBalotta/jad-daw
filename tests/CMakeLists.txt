file(GLOB_RECURSE LPC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(lpc_tests ${LPC_TEST_SOURCES})
target_link_libraries(lpc_tests PRIVATE lpc_core Catch2::Catch2WithMain)
target_compile_definitions(lpc_tests PRIVATE LPC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(TARGET lpc_cli_lib)
    target_link_libraries(lpc_tests PRIVATE lpc_cli_lib)
    target_compile_definitions(lpc_tests PRIVATE LPC_HAVE_CLI_LIB=1)
endif()
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(lpc_tests)

add_test(NAME audio_dirs_have_no_locks
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_SOURCE_DIR}/core -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_locks.cmake)
add_test(NAME lock_check_detects_violation
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data/bad_core -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_locks.cmake)
set_tests_properties(lock_check_detects_violation PROPERTIES WILL_FAIL TRUE)
