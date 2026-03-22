cmake_minimum_required(VERSION 3.16)
project(tinyfugue CXX)
set(CMAKE_CXX_STANDARD 17)

# tinymux root — two levels up from client/tf/
get_filename_component(TINYMUX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Hydra proto directory
set(HYDRA_PROTO_DIR "${TINYMUX_DIR}/proxy")

find_program(RAGEL ragel REQUIRED)
find_library(NCURSESW_LIB ncursesw REQUIRED)
find_library(PCRE2_LIB pcre2-8 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(mux SHARED IMPORTED)
set_target_properties(mux PROPERTIES
    IMPORTED_LOCATION "${TINYMUX_DIR}/mux/lib/libmux.so"
    INTERFACE_INCLUDE_DIRECTORIES "${TINYMUX_DIR}/mux/include")

# ---- Optional: gRPC/Hydra support ----
option(HYDRA_GRPC "Enable gRPC/Hydra proxy support" OFF)

if(HYDRA_GRPC)
    find_package(Protobuf REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++)

    find_program(PROTOC protoc REQUIRED)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

    # Generate protobuf and gRPC stubs from hydra.proto
    set(PROTO_SRC "${HYDRA_PROTO_DIR}/hydra.proto")
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto")
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

    set(PB_CC "${PROTO_GEN_DIR}/hydra.pb.cc")
    set(PB_H  "${PROTO_GEN_DIR}/hydra.pb.h")
    set(GRPC_CC "${PROTO_GEN_DIR}/hydra.grpc.pb.cc")
    set(GRPC_H  "${PROTO_GEN_DIR}/hydra.grpc.pb.h")

    add_custom_command(
        OUTPUT ${PB_CC} ${PB_H} ${GRPC_CC} ${GRPC_H}
        COMMAND ${PROTOC}
            --proto_path=${HYDRA_PROTO_DIR}
            --cpp_out=${PROTO_GEN_DIR}
            --grpc_out=${PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${PROTO_SRC}
        DEPENDS ${PROTO_SRC}
        COMMENT "Generating protobuf/gRPC stubs from hydra.proto"
        VERBATIM)
endif()

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

set(TF_SOURCES
    src/main.cpp src/terminal.cpp src/connection.cpp src/charset.cpp
    src/world.cpp src/command.cpp src/macro.cpp src/timer.cpp src/keybind.cpp
    src/input_lex.cpp src/script_lex.cpp src/script_parse.cpp src/regex_utils.cpp
    src/restart.cpp src/spawn.cpp
    ../shared/input_editor.cpp)

if(HYDRA_GRPC)
    list(APPEND TF_SOURCES
        src/hydra_connection.cpp
        ${PB_CC} ${GRPC_CC})
endif()

add_executable(tf ${TF_SOURCES})

target_include_directories(tf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${TINYMUX_DIR}/mux/include)

target_link_libraries(tf PRIVATE mux ${NCURSESW_LIB} ${PCRE2_LIB} OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)

if(HYDRA_GRPC)
    target_include_directories(tf PRIVATE ${PROTO_GEN_DIR} ${GRPC_INCLUDE_DIRS})
    target_link_libraries(tf PRIVATE ${GRPC_LIBRARIES} ${Protobuf_LIBRARIES})
    target_compile_definitions(tf PRIVATE HYDRA_GRPC)
endif()

set_target_properties(tf PROPERTIES
    BUILD_RPATH "${TINYMUX_DIR}/mux/lib"
    INSTALL_RPATH "${TINYMUX_DIR}/mux/lib")
