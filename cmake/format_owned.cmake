if(NOT DEFINED ROOT OR NOT DEFINED CLANG_FORMAT OR NOT DEFINED MODE)
  message(FATAL_ERROR "ROOT, CLANG_FORMAT, and MODE are required")
endif()

execute_process(
  COMMAND "${CLANG_FORMAT}" --version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "version 18\\.")
  message(FATAL_ERROR "clang-format 18 is required; got: ${version_output}${version_error}")
endif()

file(GLOB_RECURSE owned_files
  "${ROOT}/src/*.c"
  "${ROOT}/src/*.cpp"
  "${ROOT}/src/*.h"
  "${ROOT}/src/*.hpp"
  "${ROOT}/examples/*.ino"
  "${ROOT}/tests/unit/*.cpp"
  "${ROOT}/tests/integration/runtime/*.c"
  "${ROOT}/tests/integration/runtime/*.cpp"
  "${ROOT}/tests/integration/consumer/*.c"
  "${ROOT}/tests/integration/consumer/*.cpp"
)
list(FILTER owned_files EXCLUDE REGEX "/src/vendor/")
list(FILTER owned_files EXCLUDE REGEX "/generated/")

foreach(source IN LISTS owned_files)
  if(MODE STREQUAL "APPLY")
    execute_process(COMMAND "${CLANG_FORMAT}" -i "${source}" RESULT_VARIABLE result)
  elseif(MODE STREQUAL "CHECK")
    execute_process(COMMAND "${CLANG_FORMAT}" --dry-run --Werror "${source}" RESULT_VARIABLE result)
  else()
    message(FATAL_ERROR "MODE must be APPLY or CHECK")
  endif()
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Formatting failed for ${source}")
  endif()
endforeach()

message(STATUS "${MODE} formatting completed for owned Corelib sources")
