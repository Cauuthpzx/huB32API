function(add_unit_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE hub32api-core GTest::gtest_main GTest::gmock)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/tests/mocks
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_plugin_registry    core/test_plugin_registry.cpp)
add_unit_test(test_connection_pool    core/test_connection_pool.cpp)
add_unit_test(test_result_type        core/test_result_type.cpp)
add_unit_test(test_crypto_utils    core/test_crypto_utils.cpp)
add_unit_test(test_jwt_auth           auth/test_jwt_auth.cpp)
add_unit_test(test_user_role_store   auth/test_user_role_store.cpp)
add_unit_test(test_computer_ctrl_v1   api/v1/test_computer_controller.cpp)
add_unit_test(test_feature_ctrl_v1    api/v1/test_feature_controller.cpp)
add_unit_test(test_auth_ctrl_v1       api/v1/test_auth_controller.cpp)
add_unit_test(test_batch_ctrl_v2      api/v2/test_batch_controller.cpp)
add_unit_test(test_feature_plugin     plugins/test_feature_plugin.cpp)
add_unit_test(test_computer_plugin    plugins/test_computer_plugin.cpp)
add_unit_test(test_agent_registry     agent/test_agent_registry.cpp)
add_unit_test(test_agent_key_validation auth/test_agent_key_validation.cpp)
add_unit_test(test_heartbeat_monitor  agent/test_heartbeat_monitor.cpp)
add_unit_test(test_token_store_sqlite auth/test_token_store_sqlite.cpp)
add_unit_test(test_database_manager  db/test_database_manager.cpp)
target_link_libraries(test_database_manager PRIVATE SQLite::SQLite3)

add_unit_test(test_school_repository   db/test_school_repository.cpp)
target_link_libraries(test_school_repository PRIVATE SQLite::SQLite3)

add_unit_test(test_location_repository db/test_location_repository.cpp)
target_link_libraries(test_location_repository PRIVATE SQLite::SQLite3)

add_unit_test(test_computer_repository db/test_computer_repository.cpp)
target_link_libraries(test_computer_repository PRIVATE SQLite::SQLite3)

add_unit_test(test_teacher_repository db/test_teacher_repository.cpp)
target_link_libraries(test_teacher_repository PRIVATE SQLite::SQLite3)

add_unit_test(test_teacher_location_repository db/test_teacher_location_repository.cpp)
target_link_libraries(test_teacher_location_repository PRIVATE SQLite::SQLite3)
