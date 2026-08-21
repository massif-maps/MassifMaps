# The property table takes the address of every accessor thunk, so a table generated over the whole
# SDK would need the whole SDK to link. These tests generate a REDUCED table from an explicit list
# of modules instead, which keeps the link to a handful of files - and exercises the generator.
set(TEST_MODULES
  "${SDK_BASE_DIR}/all/modules/components/FogOptions.i"
)

# The SDK sources those modules' classes need. Keep this short: a case that needs a long list is a
# case that belongs behind a device check.
set(TEST_SDK_SOURCES
  "${SDK_BASE_DIR}/all/native/api/Context.cpp"
  "${SDK_BASE_DIR}/all/native/api/PropertyTable.cpp"
  # Spec.cpp holds create() and the registry; the built-in factories live in SpecFactories.cpp,
  # which is deliberately NOT here - linking it would need every source and layer constructor.
  "${SDK_BASE_DIR}/all/native/api/Spec.cpp"
  "${SDK_BASE_DIR}/all/native/core/Variant.cpp"
  "${SDK_BASE_DIR}/all/native/components/FogOptions.cpp"
  "${SDK_BASE_DIR}/all/native/graphics/Color.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Log.cpp"
)

set(TEST_TABLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/api")
list(JOIN TEST_MODULES "," TEST_MODULE_LIST)

find_program(TEST_PYTHON NAMES python3 python REQUIRED)

add_custom_command(
  OUTPUT "${TEST_TABLE_DIR}/PropertyTable.inc" "${TEST_TABLE_DIR}/PropertyAccessors.inc"
  COMMAND "${TEST_PYTHON}" "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
          "--modules" "${TEST_MODULE_LIST}"
          "--cppdir" "${SDK_BASE_DIR}/all/native"
          "--outdir" "${TEST_TABLE_DIR}"
  DEPENDS ${TEST_MODULES} "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
  WORKING_DIRECTORY "${SDK_BASE_DIR}/scripts"
  COMMENT "Generating the reduced API property table for tests"
  VERBATIM)

add_executable(api_tests
  ApiTest.cpp
  "${TEST_TABLE_DIR}/PropertyTable.inc"
  ${TEST_SDK_SOURCES})

set_source_files_properties("${TEST_TABLE_DIR}/PropertyTable.inc" PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(api_tests PRIVATE
  "${SDK_BASE_DIR}/all/native"
  "${TEST_TABLE_DIR}/.."
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  "${SDK_BASE_DIR}/libs-external/picojson"
  "${SDK_BASE_DIR}/libs-external/tinyformat")

add_test(NAME api COMMAND api_tests)
