cmake_minimum_required(VERSION 3.25)
project(zip_handler)

set(CMAKE_CXX_STANDARD 23)

add_library(zip_handler zip_handler.cpp zip_handler.h)
