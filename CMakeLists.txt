cmake_minimum_required(VERSION 3.24)

project(colocourse VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Core Quick Sql WebSockets)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Quick Sql WebSockets Test)
    find_package(Qt6 OPTIONAL_COMPONENTS Widgets)
endif()

qt_standard_project_setup()

add_subdirectory(src)

# Tests run on the host only.
if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()
