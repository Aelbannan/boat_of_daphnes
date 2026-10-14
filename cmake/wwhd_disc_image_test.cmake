# Generated WUD/WUX reader fixtures; no game data or keys.
add_executable(bluewake_wwhd_disc_image_test
    ${BLUEWAKE_REPO_ROOT}/tests/wwhd_disc_image_test.cpp
    ${BLUEWAKE_HOST_SRC}/wwhd/disc_image.cpp)
target_compile_features(bluewake_wwhd_disc_image_test PRIVATE cxx_std_17)
target_include_directories(bluewake_wwhd_disc_image_test PRIVATE ${BLUEWAKE_HOST_SRC})
add_test(NAME bluewake_wwhd_disc_image_test COMMAND bluewake_wwhd_disc_image_test)
set_tests_properties(bluewake_wwhd_disc_image_test PROPERTIES TIMEOUT 15)
