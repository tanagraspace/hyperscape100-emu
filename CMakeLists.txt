cmake_minimum_required(VERSION 3.10)
project(hyperscape100-emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

option(COVERAGE "Enable gcov coverage" OFF)
if(COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Common packet library
add_library(xs_common
    src/packets/crc32.c
    src/packets/serializer.c
    src/packets/pkt_parser.c
    src/packets/session.c
)
target_include_directories(xs_common PUBLIC src)

# Emulator library
add_library(xs_emu
    src/emu/scene_loader.c
    src/emu/control.c
    src/emu/emu_server.c
)
target_include_directories(xs_emu PUBLIC src)
target_link_libraries(xs_emu xs_common)

# Emulator binary
add_executable(emu src/emu/main.c)
target_link_libraries(emu xs_emu)

# Tests
enable_testing()

add_executable(test_packets tests/test_packets.c)
target_link_libraries(test_packets xs_common)
add_test(NAME packets COMMAND test_packets)

add_executable(test_serializer tests/test_serializer.c)
target_link_libraries(test_serializer xs_common)
add_test(NAME serializer COMMAND test_serializer)

add_executable(test_parser tests/test_parser.c)
target_link_libraries(test_parser xs_common)
add_test(NAME parser COMMAND test_parser)

add_executable(test_session tests/test_session.c)
target_link_libraries(test_session xs_common)
add_test(NAME session COMMAND test_session)

add_executable(test_scene_loader tests/test_scene_loader.c)
target_link_libraries(test_scene_loader xs_emu)
add_test(NAME scene_loader COMMAND test_scene_loader)

add_executable(test_control tests/test_control.c)
target_link_libraries(test_control xs_emu)
add_test(NAME control COMMAND test_control)

add_executable(test_full_fidelity tests/test_full_fidelity.c)
target_link_libraries(test_full_fidelity xs_common)
add_test(NAME full_fidelity COMMAND test_full_fidelity)

add_executable(test_emu_tcp tests/test_emu_tcp.c)
target_link_libraries(test_emu_tcp xs_emu pthread)
add_test(NAME emu_tcp COMMAND test_emu_tcp)
