cmake_minimum_required(VERSION 3.8)

project(cppcodec)

add_library(cppcodec INTERFACE)
target_compile_features(cppcodec INTERFACE cxx_std_11)
target_include_directories(cppcodec INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)