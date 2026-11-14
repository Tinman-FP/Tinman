get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    test_printhost.cpp
    test_fibreseek_send.cpp
    )

if (MSVC)
    target_link_libraries(${_TEST_NAME}_tests Setupapi.lib)
endif ()

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r_gui libslic3r Catch2::Catch2WithMain)

# wxWidgets' GTK port selection is normally applied in the src directory by
# wxWidgets_USE_FILE. This test target lives in a sibling directory and includes
# public GUI headers directly, so it must carry the same port definitions.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${_TEST_NAME}_tests PRIVATE __WXGTK__)
    if (SLIC3R_GTK STREQUAL "3")
        target_compile_definitions(${_TEST_NAME}_tests PRIVATE __WXGTK3__)
    endif ()
endif ()

# The FibreSeek send tests run a mock printer written in Python on 127.0.0.1.
# The interpreter is found at run time if CMake does not find one; without it
# the mock tests are skipped, or fail when TINMAN_REQUIRE_FIBRESEEK_MOCK is set (CI).
target_compile_definitions(${_TEST_NAME}_tests PRIVATE
    FIBRESEEK_MOCK_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/fibreseek_mock_moonraker.py")
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    target_compile_definitions(${_TEST_NAME}_tests PRIVATE FIBRESEEK_MOCK_PYTHON="${Python3_EXECUTABLE}")
endif ()

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

orcaslicer_copy_test_dlls()

orcaslicer_discover_tests(${_TEST_NAME}_tests)
