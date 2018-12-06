cmake_minimum_required(VERSION 3.8)

project(nlohmann_json VERSION 3.4.0 LANGUAGES CXX)

add_library(nlohmann_json INTERFACE)
target_compile_features(nlohmann_json INTERFACE cxx_std_11)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)