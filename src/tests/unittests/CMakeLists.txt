add_subdirectory(common)
add_subdirectory(iop)

if(USE_AI)
  add_subdirectory(ai)
endif(USE_AI)

if(USE_MCP)
  add_subdirectory(mcp)
endif(USE_MCP)

add_cmocka_test(test_sample
                SOURCES test_sample.c
                LINK_LIBRARIES cmocka)

# Windows: main-method requires the wrapper provided by lib-darktable
if(WIN32)
    target_link_libraries(test_sample PRIVATE lib_darktable)
    _copy_required_library(test_sample lib_darktable)
endif(WIN32)

# HDR exposure-bracket auto-alignment. Calls the public dt_hdr_alignment_* API
# exported from lib_darktable (which carries the OpenCV-linked objects); the test
# itself adapts at runtime to OpenCV-present / -absent builds.
add_cmocka_test(test_hdr_alignment
                SOURCES test_hdr_alignment.c
                LINK_LIBRARIES lib_darktable cmocka)

if(WIN32)
    _copy_required_library(test_hdr_alignment lib_darktable)
endif(WIN32)

# White-box tests for the internal (static) helpers of the HDR alignment code.
# Following test_filmicrgb.c, the implementation is #included directly for static
# access; lib_darktable supplies what it links against (OpenCV, glib, ...).
# C++ because hdr_alignment.cc is (OpenCV 4 has no C API).
add_cmocka_test(test_hdr_alignment_internal
                SOURCES test_hdr_alignment_internal.cc
                LINK_LIBRARIES lib_darktable cmocka)

if(WIN32)
    _copy_required_library(test_hdr_alignment_internal lib_darktable)
endif(WIN32)

# Tethering hardening: locale guard + camera detection regression tests.
# These two are deliberately NOT cmocka tests and do not link lib_darktable.
# They exercise the decision logic in isolation - the locale guard is a
# self-contained static helper, and the detection path is driven through
# fakes for the gphoto2 abilities list - so they build and run in a minimal
# container that has neither a camera nor the full darktable runtime. That
# is the point: a tethered developer should be able to prove the guard is
# thread-local without a camera on the desk.
#
# T4/T5 in test_locale_guard self-report SKIP when only the C locale is
# installed, because thread-local and process-global behaviour are then
# indistinguishable and a pass would claim a verification that did not
# happen.
add_executable(test_locale_guard test_locale_guard.c)
target_link_libraries(test_locale_guard PRIVATE Threads::Threads)
add_test(NAME test_locale_guard COMMAND test_locale_guard)

add_executable(test_camera_detection test_camera_detection.c)
add_test(NAME test_camera_detection COMMAND test_camera_detection)

# darktable-cli exit code contract and --results JSON schema. Links only glib:
# the schema is a contract, and a contract should be testable without a
# library, a config dir, or a camera. test_cli_results.c also renders JSON
# that the accompanying shell check re-parses with an independent parser, so a
# malformed document fails the test rather than shipping.
add_executable(test_cli_results test_cli_results.c ${CMAKE_CURRENT_SOURCE_DIR}/../../cli/results.c)
target_include_directories(test_cli_results PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../cli)
target_link_libraries(test_cli_results PRIVATE glib-2.0)
add_test(NAME test_cli_results COMMAND test_cli_results)

# The independent JSON validation described above: render a document, then
# parse it with something that is not the code that wrote it.
add_executable(test_cli_results_emit test_cli_results_emit.c ${CMAKE_CURRENT_SOURCE_DIR}/../../cli/results.c)
target_include_directories(test_cli_results_emit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../cli)
target_link_libraries(test_cli_results_emit PRIVATE glib-2.0)

add_test(NAME test_cli_results_json
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_cli_results_json.sh)
set_tests_properties(test_cli_results_json PROPERTIES DEPENDS test_cli_results_emit)
