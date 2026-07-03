cmake_minimum_required(VERSION 3.10)
project(GomokuGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gomoku_gui AI_Gomoku_GUI.cpp)
add_executable(gomoku_cli gomoku_cli.cpp)

if(WIN32)
    target_link_libraries(gomoku_gui PRIVATE gdi32 user32)
else()
    find_package(X11 REQUIRED)
    target_include_directories(gomoku_gui PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(gomoku_gui PRIVATE ${X11_LIBRARIES} Xi)
endif()
