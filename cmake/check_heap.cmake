execute_process(COMMAND "${NM}" -u "${LIBRARY}"
  OUTPUT_VARIABLE UNDEFINED_SYMBOLS RESULT_VARIABLE NM_RESULT)
if(NOT NM_RESULT EQUAL 0)
  message(FATAL_ERROR "nm failed while checking allocator dependencies")
endif()
if(UNDEFINED_SYMBOLS MATCHES "(^|[ \n])(malloc|calloc|realloc|free)([ \n]|$)")
  message(FATAL_ERROR "portable core has an allocator dependency")
endif()
message(STATUS "portable core has no allocator symbols")
