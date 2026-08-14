if(NOT DEFINED ROOT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "ROOT and OUTPUT are required")
endif()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY
  "${ROOT}/src"
  "${ROOT}/examples"
  "${ROOT}/docs"
  DESTINATION "${OUTPUT}"
)
file(COPY
  "${ROOT}/README.md"
  "${ROOT}/CHANGELOG.md"
  "${ROOT}/LICENSE"
  "${ROOT}/library.properties"
  "${ROOT}/keywords.txt"
  DESTINATION "${OUTPUT}"
)
