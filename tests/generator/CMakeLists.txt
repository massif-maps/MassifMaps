# Checks over gen-api-tables.py itself. These parse .i and .h as TEXT, so they need no SDK link -
# which is the only reason the GDAL classes can be covered here at all: nothing in this tree can
# link GDAL, and the profile gating is the part that actually needs guarding.
find_program(TEST_PYTHON NAMES python3 python REQUIRED)

add_test(NAME generator_gdal_gating
         COMMAND "${TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/test_gdal_gating.py")
