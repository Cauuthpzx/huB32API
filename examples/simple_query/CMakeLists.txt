add_executable(example-simple-query main.cpp)
target_link_libraries(example-simple-query PRIVATE hub32api-core httplib::httplib)
target_include_directories(example-simple-query PRIVATE ${CMAKE_SOURCE_DIR}/include)
