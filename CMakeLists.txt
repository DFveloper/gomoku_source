cmake_minimum_required(VERSION 3.13)
project(GomokuGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MINGW)
    add_link_options(-static)
endif()

add_executable(gomoku_gui AI_Gomoku_GUI.cpp)
add_executable(gomoku_sangin AI_Gomoku_GUI.cpp)
target_compile_definitions(gomoku_sangin PRIVATE GOMOKU_SANGIN
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(gomoku_gui PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(gomoku_cli gomoku_cli.cpp)
add_executable(engine_benchmark engine_benchmark.cpp)
add_executable(gomoku_webui AI_Gomoku_WebUI.cpp)
add_executable(gomoku_match_webui Gomoku_Engine_Match_WebUI.cpp)

target_compile_definitions(gomoku_webui PRIVATE
    GOMOKU_WEB_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/webui"
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(gomoku_match_webui PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(gomoku_gui PRIVATE Threads::Threads)
target_link_libraries(gomoku_sangin PRIVATE Threads::Threads)
target_link_libraries(gomoku_cli PRIVATE Threads::Threads)
target_link_libraries(engine_benchmark PRIVATE Threads::Threads)
target_compile_definitions(engine_benchmark PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gomoku_webui PRIVATE Threads::Threads)
target_link_libraries(gomoku_match_webui PRIVATE Threads::Threads)

enable_testing()
add_test(NAME gomoku_tactical_and_style_regression COMMAND gomoku_cli test)

if(NOT WIN32)
    add_custom_target(third_party_engines
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_third_party_engines.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building native Linux ports of the external engines"
        VERBATIM)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX)
    enable_language(RC)
    target_link_libraries(gomoku_gui PRIVATE gdi32 user32)
    target_link_libraries(gomoku_sangin PRIVATE gdi32 user32)
    target_link_libraries(gomoku_webui PRIVATE ws2_32)
    target_link_libraries(gomoku_match_webui PRIVATE ws2_32)

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
    target_include_directories(gomoku_sangin PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(gomoku_sangin PRIVATE ${X11_LIBRARIES} Xi)
endif()
