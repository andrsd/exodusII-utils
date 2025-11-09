project(exo-join)

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/contrib
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        exodusIIcpp::exodusIIcpp
        fmt::fmt
)
