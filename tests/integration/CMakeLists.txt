add_executable(test_api_integration
    test_api_v1_full.cpp
    test_api_v2_full.cpp
    test_auth_flow.cpp
)

target_link_libraries(test_api_integration PRIVATE
    hub32api-core
    hub32api-server
    GTest::gtest_main
    GTest::gmock
)

target_include_directories(test_api_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/mocks
)

add_test(NAME integration_tests COMMAND test_api_integration)
