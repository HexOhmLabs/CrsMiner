cmake_minimum_required(VERSION 3.22.1)
project(crs_sha256d LANGUAGES CXX)
add_library(crs_sha256d SHARED sha256d.cpp)
target_compile_options(crs_sha256d PRIVATE -O3 -Wall -Wextra)
