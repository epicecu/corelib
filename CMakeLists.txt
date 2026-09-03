cmake_minimum_required(VERSION 3.20)
project(corelib VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(CORELIB_BUILD_TESTS "Build native validation tests" OFF)
option(CORELIB_BUILD_CPP "Build the optional C++ wrapper" OFF)
option(CORELIB_BUILD_GATEWAY "Build the optional gateway component" OFF)
option(CORELIB_BUILD_EXAMPLES "Build the framework-neutral device consumer" OFF)
option(CORELIB_BUILD_FUZZERS "Build Clang fuzz targets" OFF)
option(CORELIB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CORELIB_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(CORELIB_ENABLE_COVERAGE "Instrument native targets for code coverage" OFF)
set(CORELIB_ETL_ROOT "" CACHE PATH "Embedded Template Library source or install root")

if(CORELIB_ENABLE_COVERAGE)
  if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "CORELIB_ENABLE_COVERAGE requires GCC and G++")
  endif()
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

add_library(corelib_device
  src/corelib/device.c
  src/protocol/pfp.c
  src/protocol/transaction.c
  src/protocol/v2/generated/transaction.pb.c
  src/vendor/nanopb/pb_common.c
  src/vendor/nanopb/pb_decode.c
  src/vendor/nanopb/pb_encode.c
)
add_library(Corelib::Device ALIAS corelib_device)
set_target_properties(corelib_device PROPERTIES EXPORT_NAME Device)
target_compile_features(corelib_device PUBLIC c_std_11)
target_include_directories(corelib_device
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(corelib_device PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
  if(CORELIB_WARNINGS_AS_ERRORS)
    target_compile_options(corelib_device PRIVATE -Werror)
  endif()
endif()

if(CORELIB_BUILD_GATEWAY)
  add_library(corelib_gateway src/corelib/gateway.c)
  add_library(Corelib::Gateway ALIAS corelib_gateway)
  set_target_properties(corelib_gateway PROPERTIES EXPORT_NAME Gateway)
  target_compile_features(corelib_gateway PUBLIC c_std_11)
  target_compile_definitions(corelib_gateway PUBLIC CORELIB_ENABLE_GATEWAY=1)
  target_include_directories(corelib_gateway
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(corelib_gateway PUBLIC corelib_device)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(corelib_gateway PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    if(CORELIB_WARNINGS_AS_ERRORS)
      target_compile_options(corelib_gateway PRIVATE -Werror)
    endif()
  endif()
endif()

if(CORELIB_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(CORELIB_BUILD_CPP)
  find_path(CORELIB_ETL_INCLUDE_DIR etl/span.h
    HINTS "${CORELIB_ETL_ROOT}/include" "${CORELIB_ETL_ROOT}/src"
    REQUIRED
  )
  add_library(corelib_device_cpp INTERFACE)
  add_library(Corelib::DeviceCpp ALIAS corelib_device_cpp)
  set_target_properties(corelib_device_cpp PROPERTIES EXPORT_NAME DeviceCpp)
  target_compile_features(corelib_device_cpp INTERFACE cxx_std_14)
  target_include_directories(corelib_device_cpp INTERFACE
    $<BUILD_INTERFACE:${CORELIB_ETL_INCLUDE_DIR}>
  )
  target_link_libraries(corelib_device_cpp INTERFACE corelib_device)
endif()

if(CORELIB_BUILD_GATEWAY AND CORELIB_BUILD_CPP)
  add_library(corelib_gateway_cpp INTERFACE)
  add_library(Corelib::GatewayCpp ALIAS corelib_gateway_cpp)
  set_target_properties(corelib_gateway_cpp PROPERTIES EXPORT_NAME GatewayCpp)
  target_compile_features(corelib_gateway_cpp INTERFACE cxx_std_14)
  target_include_directories(corelib_gateway_cpp INTERFACE
    $<BUILD_INTERFACE:${CORELIB_ETL_INCLUDE_DIR}>
  )
  target_link_libraries(corelib_gateway_cpp INTERFACE
    corelib_gateway corelib_device_cpp)
endif()

if(CORELIB_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)

  function(corelib_add_unit_test target source library labels)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_link_libraries(${target} PRIVATE ${library} GTest::gmock_main)
    gtest_discover_tests(${target}
      DISCOVERY_MODE PRE_TEST
      PROPERTIES LABELS "unit;${labels}"
    )
  endfunction()

  corelib_add_unit_test(corelib_device_c_unit_tests
    tests/unit/device_c_test.cpp corelib_device "device;c")
  corelib_add_unit_test(corelib_device_failure_unit_tests
    tests/unit/device_failure_test.cpp corelib_device "device;c")
  corelib_add_unit_test(corelib_protocol_unit_tests
    tests/unit/protocol_test.cpp corelib_device "protocol;c")
  if(CORELIB_BUILD_GATEWAY)
    corelib_add_unit_test(corelib_gateway_c_unit_tests
      tests/unit/gateway_c_test.cpp corelib_gateway "gateway;c")
  endif()
  if(CORELIB_BUILD_CPP)
    corelib_add_unit_test(corelib_device_cpp_unit_tests
      tests/unit/device_cpp_test.cpp corelib_device_cpp "device;cpp")
    if(CORELIB_BUILD_GATEWAY)
      corelib_add_unit_test(corelib_gateway_cpp_unit_tests
        tests/unit/gateway_cpp_test.cpp corelib_gateway_cpp "gateway;cpp")
    endif()
  endif()

  add_executable(corelib_device_c_runtime tests/integration/runtime/device_c_runtime.c)
  target_link_libraries(corelib_device_c_runtime PRIVATE corelib_device)
  add_test(NAME corelib_device_c_runtime COMMAND corelib_device_c_runtime)
  set_tests_properties(corelib_device_c_runtime PROPERTIES LABELS "runtime;device;c")

  add_executable(corelib_protocol_conformance
    tests/integration/runtime/protocol_conformance.c)
  target_link_libraries(corelib_protocol_conformance PRIVATE corelib_device)
  add_test(NAME corelib_protocol_conformance COMMAND corelib_protocol_conformance)
  set_tests_properties(corelib_protocol_conformance PROPERTIES LABELS "runtime;conformance;c")

  add_executable(corelib_pico_device_runtime
    tests/integration/runtime/pico_device_runtime.c
    tests/hardware/pico-hid/src/corelib_self_test.c
    tests/hardware/pico-hid/src/test_device.c
    tests/hardware/pico-hid/src/generated/common.pb.c
    tests/hardware/pico-hid/src/generated/shares.pb.c
  )
  target_include_directories(corelib_pico_device_runtime PRIVATE
    tests/hardware/pico-hid/src
  )
  target_link_libraries(corelib_pico_device_runtime PRIVATE corelib_device)
  add_test(NAME corelib_pico_device_runtime COMMAND corelib_pico_device_runtime)
  set_tests_properties(corelib_pico_device_runtime PROPERTIES LABELS "runtime;device;c")

  if(CORELIB_BUILD_GATEWAY)
    add_executable(corelib_gateway_c_runtime
      tests/integration/runtime/gateway_c_runtime.c)
    target_link_libraries(corelib_gateway_c_runtime PRIVATE corelib_gateway)
    add_test(NAME corelib_gateway_c_runtime COMMAND corelib_gateway_c_runtime)
    set_tests_properties(corelib_gateway_c_runtime PROPERTIES LABELS "runtime;gateway;c")
  endif()

  if(CORELIB_BUILD_CPP)
    add_executable(corelib_device_cpp_runtime
      tests/integration/runtime/device_cpp_runtime.cpp)
    target_link_libraries(corelib_device_cpp_runtime PRIVATE corelib_device_cpp)
    set_target_properties(corelib_device_cpp_runtime PROPERTIES
      CXX_STANDARD 14
      CXX_STANDARD_REQUIRED YES
      CXX_EXTENSIONS NO
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(corelib_device_cpp_runtime PRIVATE
        -fno-exceptions -fno-rtti
      )
    endif()
    add_test(NAME corelib_device_cpp_runtime COMMAND corelib_device_cpp_runtime)
    set_tests_properties(corelib_device_cpp_runtime PROPERTIES LABELS "runtime;device;cpp")
    add_test(NAME corelib_device_cpp_runtime_symbols
      COMMAND ${CMAKE_COMMAND}
        -DNM=${CMAKE_NM}
        -DEXECUTABLE=$<TARGET_FILE:corelib_device_cpp_runtime>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_cpp_runtime.cmake
    )
    set_tests_properties(corelib_device_cpp_runtime_symbols PROPERTIES LABELS "runtime;device;cpp")

    if(CORELIB_BUILD_GATEWAY)
      add_executable(corelib_gateway_cpp_runtime
        tests/integration/runtime/gateway_cpp_runtime.cpp)
      target_link_libraries(corelib_gateway_cpp_runtime PRIVATE corelib_gateway_cpp)
      set_target_properties(corelib_gateway_cpp_runtime PROPERTIES
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
      if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(corelib_gateway_cpp_runtime PRIVATE -fno-exceptions -fno-rtti)
      endif()
      add_test(NAME corelib_gateway_cpp_runtime COMMAND corelib_gateway_cpp_runtime)
      set_tests_properties(corelib_gateway_cpp_runtime PROPERTIES LABELS "runtime;gateway;cpp")
      add_test(NAME corelib_gateway_cpp_runtime_symbols
        COMMAND ${CMAKE_COMMAND}
          -DNM=${CMAKE_NM}
          -DEXECUTABLE=$<TARGET_FILE:corelib_gateway_cpp_runtime>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_cpp_runtime.cmake
      )
      set_tests_properties(corelib_gateway_cpp_runtime_symbols PROPERTIES LABELS "runtime;gateway;cpp")
    endif()
  endif()
endif()

if(CORELIB_BUILD_EXAMPLES)
  add_executable(corelib_device_consumer
    tests/integration/consumer/build-tree/main.c)
  target_link_libraries(corelib_device_consumer PRIVATE corelib_device)
endif()

if(CORELIB_BUILD_FUZZERS)
  add_executable(corelib_frame_fuzz tests/fuzz/frame_fuzz.c)
  target_compile_options(corelib_frame_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
  )
  target_link_options(corelib_frame_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
  )
  target_link_libraries(corelib_frame_fuzz PRIVATE corelib_device)
  if(CORELIB_BUILD_GATEWAY)
    add_executable(corelib_gateway_fuzz tests/fuzz/gateway_fuzz.c)
    target_compile_options(corelib_gateway_fuzz PRIVATE
      -fsanitize=fuzzer,address,undefined)
    target_link_options(corelib_gateway_fuzz PRIVATE
      -fsanitize=fuzzer,address,undefined)
    target_link_libraries(corelib_gateway_fuzz PRIVATE corelib_gateway)
  endif()
endif()

install(TARGETS corelib_device EXPORT CorelibTargets)
if(CORELIB_BUILD_GATEWAY)
  install(TARGETS corelib_gateway EXPORT CorelibTargets)
  if(CORELIB_BUILD_CPP)
    install(TARGETS corelib_gateway_cpp EXPORT CorelibTargets)
  endif()
endif()
install(FILES src/Corelib.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY src/corelib/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/corelib FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
if(CORELIB_BUILD_CPP)
  install(TARGETS corelib_device_cpp EXPORT CorelibTargets)
endif()
install(EXPORT CorelibTargets
  NAMESPACE Corelib::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Corelib
)
configure_package_config_file(cmake/CorelibConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CorelibConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Corelib
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/CorelibConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CorelibConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/CorelibConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Corelib
)
