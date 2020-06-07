cmake_minimum_required(VERSION 2.8)

project(server)

#include_directories(./)

#auX_source_directory(./ SRC_LIST)

add_executable(server httpd.c)

target_link_libraries(server pthread)

