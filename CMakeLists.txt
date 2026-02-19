cmake_minimum_required(VERSION 3.21)
project(ktikz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_DEFAULT_MAJOR_VERSION 6)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Pdf)
find_package(KF6 REQUIRED COMPONENTS TextEditor XmlGui)

add_executable(ktikz
    src/main.cpp
)

target_link_libraries(ktikz PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Pdf
    KF6::TextEditor
    KF6::XmlGui
)

install(TARGETS ktikz RUNTIME DESTINATION ${KDE_INSTALL_BINDIR})
