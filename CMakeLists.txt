cmake_minimum_required(VERSION 3.20)
project(webgpu_bcmc_data_generator)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(zfp REQUIRED)
# Include glm as an external project
include(cmake/glm.cmake)

add_executable(zfp_make_test_data 
    zfp_make_test_data.cpp)

set_target_properties(zfp_make_test_data PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_link_libraries(zfp_make_test_data PUBLIC
    zfp::zfp
    glm)


