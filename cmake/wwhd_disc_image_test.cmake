# Generated WUD/WUX reader fixtures; no game data or keys.
add_executable(bluewake_wwhd_disc_image_test
    ${BLUEWAKE_REPO_ROOT}/tests/wwhd_disc_image_test.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/disc_image.cpp)
target_compile_features(bluewake_wwhd_disc_image_test PRIVATE cxx_std_17)
target_include_directories(bluewake_wwhd_disc_image_test PRIVATE ${BLUEWAKE_HOST_SRC})
add_test(NAME bluewake_wwhd_disc_image_test COMMAND bluewake_wwhd_disc_image_test)
set_tests_properties(bluewake_wwhd_disc_image_test PROPERTIES TIMEOUT 15)

include("${CMAKE_CURRENT_LIST_DIR}/wwhd_disc_crypto.cmake")
add_executable(bluewake_wwhd_disc_crypto_test ${BLUEWAKE_REPO_ROOT}/tests/wwhd_disc_crypto_test.cpp)
target_compile_features(bluewake_wwhd_disc_crypto_test PRIVATE cxx_std_17)
target_include_directories(bluewake_wwhd_disc_crypto_test PRIVATE ${BLUEWAKE_HOST_SRC})
target_link_libraries(bluewake_wwhd_disc_crypto_test PRIVATE bluewake_wwhd_disc_crypto)
add_test(NAME bluewake_wwhd_disc_crypto_test COMMAND bluewake_wwhd_disc_crypto_test)
set_tests_properties(bluewake_wwhd_disc_crypto_test PROPERTIES TIMEOUT 15)

# Python is used only to create independent encrypted test data, never by the
# app. Developers can select their test venv without changing Dawn's Python.
set(BLUEWAKE_WWHD_TEST_PYTHON "" CACHE FILEPATH "Python with pycryptodome for HD disc test fixtures")
if(NOT BLUEWAKE_WWHD_TEST_PYTHON)
    find_program(BLUEWAKE_WWHD_TEST_PYTHON_DEFAULT NAMES python3 python REQUIRED)
    set(BLUEWAKE_WWHD_TEST_PYTHON "${BLUEWAKE_WWHD_TEST_PYTHON_DEFAULT}")
endif()
set(_wwhd_fixtures "${BLUEWAKE_REPO_ROOT}/build/wwhd-disc-import/fixtures")
add_executable(bluewake_wwhd_wiiu_disc_test
    ${BLUEWAKE_REPO_ROOT}/tests/wwhd_wiiu_disc_test.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/wiiu_disc.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/disc_image.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/formats.cpp)
target_compile_features(bluewake_wwhd_wiiu_disc_test PRIVATE cxx_std_17)
target_include_directories(bluewake_wwhd_wiiu_disc_test PRIVATE ${BLUEWAKE_HOST_SRC})
target_link_libraries(bluewake_wwhd_wiiu_disc_test PRIVATE bluewake_wwhd_disc_crypto xxhash)
add_test(NAME bluewake_wwhd_disc_fixtures COMMAND "${BLUEWAKE_WWHD_TEST_PYTHON}"
    -B "${BLUEWAKE_REPO_ROOT}/tests/wwhd_disc_fixture.py" "${_wwhd_fixtures}")
set_tests_properties(bluewake_wwhd_disc_fixtures PROPERTIES FIXTURES_SETUP wwhd_disc TIMEOUT 60)
add_test(NAME bluewake_wwhd_wiiu_disc_test COMMAND bluewake_wwhd_wiiu_disc_test "${_wwhd_fixtures}")
set_tests_properties(bluewake_wwhd_wiiu_disc_test PROPERTIES FIXTURES_REQUIRED wwhd_disc TIMEOUT 30)

# Exercise the complete image-to-PNG pipeline with synthetic BTI/BFRES data.
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
add_executable(bluewake_wwhd_native_import
    ${BLUEWAKE_REPO_ROOT}/tests/wwhd_native_import.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/formats.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/addrlib.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/hd.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/imaging.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/sha256.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/ui.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/pack.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/disc_image.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/wiiu_disc.cpp)
target_compile_features(bluewake_wwhd_native_import PRIVATE cxx_std_17)
target_include_directories(bluewake_wwhd_native_import PRIVATE ${BLUEWAKE_HOST_SRC})
target_link_libraries(bluewake_wwhd_native_import PRIVATE
    bluewake_wwhd_disc_crypto xxhash ZLIB::ZLIB Threads::Threads)
add_test(NAME bluewake_wwhd_disc_import_test COMMAND "${BLUEWAKE_WWHD_TEST_PYTHON}"
    -B "${BLUEWAKE_REPO_ROOT}/tests/wwhd_disc_import_test.py" "$<TARGET_FILE:bluewake_wwhd_native_import>")
set_tests_properties(bluewake_wwhd_disc_import_test PROPERTIES TIMEOUT 60)
