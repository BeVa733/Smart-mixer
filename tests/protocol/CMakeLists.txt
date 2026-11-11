cmake_minimum_required(VERSION 3.16)
project(smart_mixer_protocol_tests LANGUAGES CXX)
enable_testing()

add_executable(protocol_tests
    test_protocol.cpp
    ../../shared/SmartMixerProtocol/src/SmartMixerProtocol.cpp)
target_include_directories(protocol_tests PRIVATE
    ../../shared/SmartMixerProtocol/src)
set_target_properties(protocol_tests PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
option(ENABLE_SANITIZERS "Check parser memory and undefined behavior" OFF)
if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(protocol_tests PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(protocol_tests PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME protocol_tests COMMAND protocol_tests)
