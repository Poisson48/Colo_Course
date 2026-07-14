cmake_minimum_required(VERSION 3.24)

project(colocourse VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Sql WebSockets Test)

qt_standard_project_setup()
enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
