cmake_minimum_required(VERSION 3.10)
project(GomokuGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gomoku_gui AI_Gomoku_GUI.cpp)
add_executable(gomoku_cli gomoku_cli.cpp)

find_package(Threads REQUIRED)
target_link_libraries(gomoku_gui PRIVATE Threads::Threads)
target_link_libraries(gomoku_cli PRIVATE Threads::Threads)

enable_testing()
add_test(NAME gomoku_tactical_and_style_regression COMMAND gomoku_cli test)

if(WIN32)
    enable_language(RC)
    target_link_libraries(gomoku_gui PRIVATE gdi32 user32)

    add_executable(carbon_classic_engine
        third_party/carbon-src/AICarbon.cpp
        third_party/carbon-src/AICarbonMove.cpp
        third_party/carbon-src/AIwrapper.cpp
        third_party/carbon-src/Random.cpp
        third_party/carbon-src/pisqpipe.cpp)
    set_target_properties(carbon_classic_engine PROPERTIES
        OUTPUT_NAME pbrain-carbon
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/carbon-src")
    if(MINGW)
        target_compile_options(carbon_classic_engine PRIVATE -w)
    endif()

    add_executable(gomoku_engine_match WIN32
        Gomoku_Engine_Match_GUI.cpp
        engine_match_resources.rc)
    add_dependencies(gomoku_engine_match carbon_classic_engine)
    target_link_libraries(gomoku_engine_match PRIVATE gdi32 user32 Threads::Threads)
else()
    find_package(X11 REQUIRED)
    target_include_directories(gomoku_gui PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(gomoku_gui PRIVATE ${X11_LIBRARIES} Xi)
endif()
