add_executable(sioxx_basic_client main.cpp)
target_link_libraries(sioxx_basic_client PRIVATE sioxx::sioxx)
set_target_properties(sioxx_basic_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
