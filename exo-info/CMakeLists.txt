project(exo-info)

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/contrib
        ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        exodusIIcpp::exodusIIcpp
        fmt::fmt
)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT exodusII-utils-targets
    RUNTIME DESTINATION bin
)
