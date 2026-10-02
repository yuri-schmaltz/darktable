include_directories(${DARKTABLE_BINDIR})
# results.c holds the exit code contract and the --results JSON schema
# (dev-doc/automation-api-spec.md 2.4-2.5). Kept in its own translation
# unit so the schema can be unit-tested without linking the whole CLI.
add_executable(darktable-cli main.c results.c)

set_target_properties(darktable-cli PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(darktable-cli lib_darktable whereami)

if (WIN32)
  _detach_debuginfo (darktable-cli bin)
else()
    set_target_properties(darktable-cli
                          PROPERTIES
                          INSTALL_RPATH ${RPATH_ORIGIN}/${REL_BIN_TO_LIBDIR}
                          RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})
endif(WIN32)

install(TARGETS darktable-cli DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DTApplication)
