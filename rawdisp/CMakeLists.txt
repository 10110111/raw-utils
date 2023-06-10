cmake_minimum_required(VERSION 3.10.0)
project(RawDisp VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -Wall -Wextra")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 5.9 REQUIRED Core OpenGL Concurrent)
include(FindPkgConfig)
pkg_check_modules(exiv2 REQUIRED IMPORTED_TARGET exiv2)
pkg_check_modules(libraw REQUIRED IMPORTED_TARGET libraw)

add_executable(rawdisp
                rawdisp.cpp
                RawHistogram.cpp
                EXIFDisplay.cpp
                ImageCanvas.cpp
                ToolsWidget.cpp
                Manipulator.cpp
                MainWindow.cpp
                FileList.cpp
              )
include_directories(PkgConfig::exiv2)
link_directories(PkgConfig::exiv2)
include_directories(PkgConfig::libraw)
link_directories(PkgConfig::libraw)
target_link_libraries(rawdisp Qt5::Core Qt5::OpenGL Qt5::Concurrent GL ${exiv2_LIBRARIES} ${libraw_LIBRARIES})
