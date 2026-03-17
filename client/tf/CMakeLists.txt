cmake_minimum_required(VERSION 3.16)
project(tinyfugue CXX)
set(CMAKE_CXX_STANDARD 17)

# tinymux root — two levels up from client/tf/
get_filename_component(TINYMUX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_program(RAGEL ragel REQUIRED)
find_library(NCURSESW_LIB ncursesw REQUIRED)
find_library(PCRE2_LIB pcre2-8 REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(mux SHARED IMPORTED)
set_target_properties(mux PROPERTIES
    IMPORTED_LOCATION "${TINYMUX_DIR}/mux/lib/libmux.so"
    INTERFACE_INCLUDE_DIRECTORIES "${TINYMUX_DIR}/mux/include")

# Ragel -G2: generate goto-driven DFAs from .rl sources
foreach(RL_NAME input_lex script_lex)
    set(RL_INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/src/${RL_NAME}.rl)
    set(RL_OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/${RL_NAME}.cpp)
    add_custom_command(
        OUTPUT  ${RL_OUTPUT}
        COMMAND ${RAGEL} -G2 -o ${RL_OUTPUT} ${RL_INPUT}
        DEPENDS ${RL_INPUT}
        COMMENT "Ragel -G2: ${RL_NAME}.rl -> ${RL_NAME}.cpp"
        VERBATIM)
endforeach()

add_executable(tf
    src/main.cpp src/terminal.cpp src/connection.cpp
    src/world.cpp src/command.cpp src/macro.cpp src/timer.cpp src/keybind.cpp
    src/input_lex.cpp src/script_lex.cpp src/script_parse.cpp src/regex_utils.cpp)

target_include_directories(tf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${TINYMUX_DIR}/mux/include)
target_link_libraries(tf PRIVATE mux ${NCURSESW_LIB} ${PCRE2_LIB} OpenSSL::SSL OpenSSL::Crypto)
set_target_properties(tf PROPERTIES
    BUILD_RPATH "${TINYMUX_DIR}/mux/lib"
    INSTALL_RPATH "${TINYMUX_DIR}/mux/lib")
