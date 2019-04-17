cmake_minimum_required(VERSION 3.1.0)
project(combine-exposures CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-missing-field-initializers -Werror=return-type")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt5 5.10 REQUIRED Core Widgets OpenGL)
find_package(PkgConfig REQUIRED)
include_directories(${CMAKE_SOURCE_DIR})

include(CheckIncludeFileCXX)
check_include_file_cxx("glm/glm.hpp" HAVE_GLM)
if(NOT HAVE_GLM)
    unset(HAVE_GLM CACHE)
    message(FATAL_ERROR "GLM was not found")
endif()

set(GLAD_DIR "${CMAKE_SOURCE_DIR}/glad")
include_directories("${GLAD_DIR}/include")
if(UNIX)
    set(GLAD_LIBS dl)
endif()

add_executable(combine-exposures combine-exposures.cpp
                                 ${GLAD_DIR}/src/glad.c
                                 FramesModel.cpp
                                 FrameView.cpp
                                 MainWindow.cpp
                                 MainWindow.ui)
target_link_libraries(combine-exposures Qt5::Core Qt5::Widgets Qt5::OpenGL ${GLAD_LIBS} stdc++fs raw)
