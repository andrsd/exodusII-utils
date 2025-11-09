cmake_minimum_required(VERSION 3.18)

project(exodusII-utils
    LANGUAGES CXX
    VERSION 0.1.0
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(exodusIIcpp 3 REQUIRED)
find_package(fmt 11 REQUIRED)

add_subdirectory(exo-join)
add_subdirectory(exo-info)
