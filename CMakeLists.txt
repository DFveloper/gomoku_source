cmake_minimum_required(VERSION 3.10)
project(GomokuGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gomoku_gui AI_Gomoku_GUI.cpp)
target_compile_definitions(gomoku_gui PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(gomoku_cli gomoku_cli.cpp)
add_executable(engine_benchmark engine_benchmark.cpp)
add_executable(gomoku_webui AI_Gomoku_WebUI.cpp)
add_executable(gomoku_match_webui Gomoku_Engine_Match_WebUI.cpp)

# Engine-development tools: a paired A/B referee against the frozen baseline
# revision in tools/fixtures/, and a root soundness check for iterative
# deepening.  Both are header-only and need no third-party runtime.
add_executable(ab_match tools/ab_match.cpp)
add_executable(id_soundness tools/id_soundness.cpp)
add_executable(tactics_suite tools/tactics_suite.cpp)
add_executable(duel tools/duel.cpp)
add_executable(ensemble_invariants tools/ensemble_invariants.cpp)
target_compile_definitions(duel PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ensemble_invariants PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
foreach(tool_target ab_match id_soundness tactics_suite duel ensemble_invariants)
    target_include_directories(${tool_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools)
endforeach()

target_compile_definitions(gomoku_webui PRIVATE
    GOMOKU_WEB_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/webui"
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(gomoku_match_webui PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(gomoku_gui PRIVATE Threads::Threads)
target_link_libraries(gomoku_cli PRIVATE Threads::Threads)
target_link_libraries(engine_benchmark PRIVATE Threads::Threads)
target_compile_definitions(engine_benchmark PRIVATE
    GOMOKU_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gomoku_webui PRIVATE Threads::Threads)
target_link_libraries(gomoku_match_webui PRIVATE Threads::Threads)
foreach(tool_target ab_match id_soundness tactics_suite duel ensemble_invariants)
    target_link_libraries(${tool_target} PRIVATE Threads::Threads)
endforeach()

enable_testing()
add_test(NAME gomoku_tactical_and_style_regression COMMAND gomoku_cli test)
add_test(NAME gomoku_iterative_deepening_root_soundness
         COMMAND id_soundness 2 4 2)
add_test(NAME gomoku_tactics_suite COMMAND tactics_suite 1 2)
# The two properties that make expert delegation shippable: it degrades to the
# local engine when no expert exists, and a proof always outranks an expert.
add_test(NAME gomoku_ensemble_invariants COMMAND ensemble_invariants)

if(NOT WIN32)
    add_custom_target(third_party_engines
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_third_party_engines.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building native Linux ports of the external engines"
        VERBATIM)
endif()

if(WIN32)
    enable_language(RC)
    target_link_libraries(gomoku_gui PRIVATE gdi32 user32)
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
endif()
