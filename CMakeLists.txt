cmake_minimum_required(VERSION 3.20)

project(
  linx-model
  VERSION 0.1.0
  DESCRIPTION "Queue-based cycle-accurate simulation framework"
  LANGUAGES CXX)

include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LINX_MODEL_ENABLE_SANITIZERS "Enable ASan, LSan, and UBSan" OFF)
option(LINX_MODEL_BUILD_DOCS "Enable Doxygen documentation target" OFF)

add_library(
  linx_model
  STATIC
  src/elf_loader.cpp
  src/emulator/compare.cpp
  src/emulator/execution_context.cpp
  src/emulator/minst_record.cpp
  src/emulator/reference_executor.cpp
  src/emulator/state.cpp
  src/isa/codec.cpp
  src/isa/disasm.cpp
  src/isa/generated_tables.cpp
  src/isa/minst.cpp
  src/logging.cpp
  src/program_image.cpp
  src/sim_assert.cpp
  src/sim_main.cpp
  src/validation.cpp)

add_library(linx::model ALIAS linx_model)

add_custom_target(
  gen-isa-codec
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/isa/gen_minst_codec.py"
          --spec "${CMAKE_CURRENT_SOURCE_DIR}/../../isa/v0.4/linxisa-v0.4.json"
          --header "${CMAKE_CURRENT_SOURCE_DIR}/include/linx/model/isa/generated_tables.hpp"
          --source "${CMAKE_CURRENT_SOURCE_DIR}/src/isa/generated_tables.cpp"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Regenerating committed LinxISA v0.4 Minst codec tables")

target_include_directories(
  linx_model
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(linx_model PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(
    linx_model
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic)
endif()

if(LINX_MODEL_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set(LINX_MODEL_SANITIZERS address undefined)
  if(NOT APPLE)
    list(APPEND LINX_MODEL_SANITIZERS leak)
  endif()
  list(JOIN LINX_MODEL_SANITIZERS "," LINX_MODEL_SANITIZER_FLAGS)
  target_compile_options(
    linx_model
    PUBLIC
      -fsanitize=${LINX_MODEL_SANITIZER_FLAGS}
      -fno-omit-frame-pointer)
  target_link_options(linx_model PUBLIC -fsanitize=${LINX_MODEL_SANITIZER_FLAGS})
endif()

function(linx_model_add_test target_name source_file label)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE linx_model)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      ${target_name}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic)
  endif()
add_test(NAME ${target_name} COMMAND ${target_name})
  set_tests_properties(${target_name} PROPERTIES LABELS "${label}")
endfunction()

linx_model_add_test(linx_model_ut_logging tests/unit/logging_test.cpp unit)
linx_model_add_test(linx_model_ut_emulator tests/unit/emulator_test.cpp unit)
linx_model_add_test(linx_model_ut_minst tests/unit/minst_test.cpp unit)
linx_model_add_test(linx_model_ut_program_loader tests/unit/program_loader_test.cpp unit)
linx_model_add_test(linx_model_ut_sim_queue tests/unit/sim_queue_test.cpp unit)
linx_model_add_test(linx_model_st_module_pipeline tests/system/module_pipeline_test.cpp system)
linx_model_add_test(linx_model_st_sim_system tests/system/sim_system_test.cpp system)
linx_model_add_test(linx_model_check_model_validation tests/checks/model_validation_test.cpp checks)
add_test(
  NAME linx_model_check_llvm_elf_fixtures
  COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/checks/check_llvm_elf_fixtures.py
    --root
    ${CMAKE_CURRENT_SOURCE_DIR}
    --cli
    $<TARGET_FILE:linx_model_cli>
    --validator
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/avs/validate_minst_schema.py
    --manifest
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/llvm_elf/manifest.json)
set_tests_properties(linx_model_check_llvm_elf_fixtures PROPERTIES LABELS "checks")

add_executable(linx_model_cli src/linx_model_cli.cpp)
target_link_libraries(linx_model_cli PRIVATE linx_model)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(
    linx_model_cli
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic)
endif()

find_program(CLANG_FORMAT_BIN clang-format)
if(CLANG_FORMAT_BIN)
  add_custom_target(
    format-check
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/run_clang_format_check.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running clang-format dry-run checks")
endif()

find_program(CLANG_TIDY_BIN clang-tidy)
if(CLANG_TIDY_BIN)
  add_custom_target(
    lint-cpp
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/run_clang_tidy.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running clang-tidy checks")
endif()

find_program(MARKDOWNLINT_BIN markdownlint-cli2)
if(MARKDOWNLINT_BIN)
  add_custom_target(
    lint-md
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/run_markdownlint.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running markdownlint checks")
endif()

find_program(DOXYGEN_BIN doxygen)
if(LINX_MODEL_BUILD_DOCS AND DOXYGEN_BIN)
  add_custom_target(
    docs
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/run_doxygen.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating Doxygen documentation")
endif()
