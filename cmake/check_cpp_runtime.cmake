execute_process(COMMAND "${NM}" -u "${EXECUTABLE}"
  OUTPUT_VARIABLE UNDEFINED_SYMBOLS RESULT_VARIABLE NM_RESULT)
if(NOT NM_RESULT EQUAL 0)
  message(FATAL_ERROR "nm failed while checking C++ runtime dependencies")
endif()
if(UNDEFINED_SYMBOLS MATCHES "(_Znwm|_Znam|_ZdlPv|_ZdaPv|__cxa_throw|__gxx_personality)")
  message(FATAL_ERROR "C++ facade test has allocation or exception dependencies")
endif()
message(STATUS "C++ facade test has no allocation or exception symbols")
