# Unofficial Linux controller for Moondrop Space Travel earbuds (GAIA v3 over RFCOMM).
#
# Build (Debian/Ubuntu: qt6-base-dev qt6-connectivity-dev libqt6bluetooth6-bin cmake g++):
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
# Run:   ./build/moondrop-link          (pair and connect the earbuds in your desktop first)
# Try:   ./build/moondrop-link --demo   (simulated earbuds, no Bluetooth needed)

cmake_minimum_required(VERSION 3.16)

project(moondrop-link-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(MOONDROP_BUILD_APP "Build the Qt Widgets application" ON)
option(MOONDROP_BUILD_TESTS "Build the unit tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

# Protocol core: depends on QtCore only so it can be tested without Bluetooth hardware.
add_library(moondrop_core STATIC
    src/gaia/gaiaframe.cpp
    src/gaia/gaiaframe.h
    src/gaia/gaiaprotocol.cpp
    src/gaia/gaiaprotocol.h
    src/gaia/gaiasession.cpp
    src/gaia/gaiasession.h
    src/gaia/touchmapping.cpp
    src/gaia/touchmapping.h
    src/sim/simulateddevice.cpp
    src/sim/simulateddevice.h
)
target_include_directories(moondrop_core PUBLIC src)
target_link_libraries(moondrop_core PUBLIC Qt6::Core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(moondrop_core PRIVATE -Wall -Wextra)
endif()

if(MOONDROP_BUILD_APP)
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Bluetooth)
add_executable(moondrop-link
    src/app/main.cpp
    src/app/bluetoothlink.cpp
    src/app/bluetoothlink.h
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/touchpage.cpp
    src/app/touchpage.h
    src/app/eqpage.cpp
    src/app/eqpage.h
)
target_link_libraries(moondrop-link PRIVATE moondrop_core Qt6::Widgets Qt6::Bluetooth)
target_compile_definitions(moondrop-link PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(moondrop-link PRIVATE -Wall -Wextra)
endif()

install(TARGETS moondrop-link RUNTIME DESTINATION bin)
install(FILES packaging/moondrop-link.desktop DESTINATION share/applications)
endif()

if(MOONDROP_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(test_name tst_gaiaframe tst_gaiaprotocol tst_touchmapping tst_gaiasession)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE moondrop_core Qt6::Test)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
