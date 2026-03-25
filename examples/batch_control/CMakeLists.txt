add_executable(example-batch-control main.cpp)
target_link_libraries(example-batch-control PRIVATE veyon32api-core httplib::httplib)
target_include_directories(example-batch-control PRIVATE ${CMAKE_SOURCE_DIR}/include)
