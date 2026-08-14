if(NOT DEFINED ROOT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "ROOT and OUTPUT are required")
endif()

set(local_binary "${OUTPUT}/usr/bin/clang-format-18")
if(EXISTS "${local_binary}")
  execute_process(COMMAND "${local_binary}" --version OUTPUT_VARIABLE local_version OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(local_version MATCHES "version 18\\.")
    message(STATUS "Using workspace ClangFormat: ${local_binary}")
    return()
  endif()
endif()

find_program(system_clang_format NAMES clang-format-18)
if(system_clang_format)
  file(MAKE_DIRECTORY "${OUTPUT}/usr/bin")
  file(CREATE_LINK "${system_clang_format}" "${local_binary}" SYMBOLIC)
  message(STATUS "Using system ClangFormat 18 through ${local_binary}")
  return()
endif()

find_program(apt_get NAMES apt-get)
find_program(dpkg_deb NAMES dpkg-deb)
if(NOT apt_get OR NOT dpkg_deb)
  message(FATAL_ERROR "ClangFormat 18 was not found. Install it or run with CLANG_FORMAT=/absolute/path/to/clang-format-18")
endif()

set(package_directory "${OUTPUT}/packages")
file(MAKE_DIRECTORY "${package_directory}")
execute_process(
  COMMAND "${apt_get}" download clang-format-18
  WORKING_DIRECTORY "${package_directory}"
  RESULT_VARIABLE download_result
)
if(NOT download_result EQUAL 0)
  message(FATAL_ERROR "Could not download clang-format-18. Install it or provide CLANG_FORMAT explicitly")
endif()

file(GLOB packages "${package_directory}/clang-format-18_*.deb")
list(LENGTH packages package_count)
if(NOT package_count EQUAL 1)
  message(FATAL_ERROR "Expected one downloaded clang-format-18 package, found ${package_count}")
endif()
list(GET packages 0 package)
execute_process(COMMAND "${dpkg_deb}" -x "${package}" "${OUTPUT}" RESULT_VARIABLE extract_result)
if(NOT extract_result EQUAL 0 OR NOT EXISTS "${local_binary}")
  message(FATAL_ERROR "Could not extract workspace ClangFormat 18")
endif()

execute_process(COMMAND "${local_binary}" --version OUTPUT_VARIABLE installed_version OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT installed_version MATCHES "version 18\\.")
  message(FATAL_ERROR "Downloaded formatter is not ClangFormat 18: ${installed_version}")
endif()
message(STATUS "Installed workspace ClangFormat 18 at ${local_binary}")
