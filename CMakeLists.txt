cmake_minimum_required(VERSION 3.25)
project(webserver_cpp11)

set(CMAKE_CXX_STANDARD 20)

include_directories(.)

add_executable(webserver_cpp11
        main.cpp)

target_link_libraries(webserver_cpp11
        pthread)
