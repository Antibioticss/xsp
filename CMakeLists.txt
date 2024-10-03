cmake_minimum_required(VERSION 3.00)
project(xsp C)

set(CMAKE_C_STANDARD 11)

add_executable(xsp xsp.c hexpatch/hexpatch.c)
