cmake_minimum_required(VERSION 3.16)
project(ktikz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt5)
endif()

add_executable(ktikz
    src/main.cpp
)

target_link_libraries(ktikz PRIVATE ${QT_PACKAGE}::Widgets)

install(TARGETS ktikz RUNTIME DESTINATION bin)
