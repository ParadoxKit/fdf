
function(add_fdf_test TEST_NAME SOURCES)
    add_executable(${TEST_NAME} ${SOURCES})
    target_link_libraries(${TEST_NAME} PRIVATE fdf)
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_23)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()


add_fdf_test(Test test.cpp)
