cmake_minimum_required(VERSION 3.16)

project(wspr_ref_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wspr_ref CONFIG REQUIRED)

add_executable(consumer main.cpp)

target_link_libraries(consumer PRIVATE wspr::wspr_ref_lib)
