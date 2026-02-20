cmake_minimum_required(VERSION 3.21)
project(qtikz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_DEFAULT_MAJOR_VERSION 6)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Pdf)

add_executable(qtikz
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow_properties.cpp
    src/mainwindow.h
    src/pdfcanvas.cpp
    src/pdfcanvas.h
    src/compileservice.cpp
    src/compileservice.h
    src/coordinateparser.cpp
    src/coordinateparser.h
    src/model.h
    src/appconfig.h
    src/settingsdialog.cpp
    src/settingsdialog.h
)

target_link_libraries(qtikz PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Pdf
)

install(TARGETS qtikz RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
