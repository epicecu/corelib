file(SHA256 "${ROOT}/proto/v2/transaction.proto" PROTO_HASH)
if(NOT DEFINED PYTHON)
  set(PYTHON python3)
endif()
set(EXPECTED_HASH "aa37ca7798cc0ba340996e12be8454a6c723978b2b67f729dad2bb7a2f3a01f7")
if(NOT PROTO_HASH STREQUAL EXPECTED_HASH)
  message(FATAL_ERROR "transaction.proto changed; regenerate sources and update the reviewed hash")
endif()
message(STATUS "transaction.proto matches the reviewed v2 schema")
set(GENERATED_DIR "/tmp/epicecu-corelib-proto-check")
file(REMOVE_RECURSE "${GENERATED_DIR}")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env PYTHONDONTWRITEBYTECODE=1
          "${PYTHON}" "${ROOT}/tools/nanopb/generator/nanopb_generator.py"
          -I "${ROOT}/proto/v2" -D "${GENERATED_DIR}"
          -f "${ROOT}/proto/v2/transaction.options"
          -L "#include \"vendor/nanopb/%s\""
          --error-on-unmatched "${ROOT}/proto/v2/transaction.proto"
  RESULT_VARIABLE GENERATE_RESULT)
if(NOT GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR "Nanopb source regeneration failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
  "${GENERATED_DIR}/transaction.pb.h"
  "${ROOT}/src/protocol/v2/generated/transaction.pb.h"
  RESULT_VARIABLE HEADER_DIFF)
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
  "${GENERATED_DIR}/transaction.pb.c"
  "${ROOT}/src/protocol/v2/generated/transaction.pb.c"
  RESULT_VARIABLE SOURCE_DIFF)
file(REMOVE_RECURSE "${GENERATED_DIR}")
if(NOT HEADER_DIFF EQUAL 0 OR NOT SOURCE_DIFF EQUAL 0)
  message(FATAL_ERROR "generated TransactionMessage sources are stale; run task proto:regen")
endif()
message(STATUS "generated Nanopb sources are deterministic and current")
