cmake_minimum_required(VERSION 3.20)
project(alpharing_core_tests LANGUAGES CXX)

enable_testing()

add_executable(alpharing_core_tests
        core_tests.cpp
        ../src/mcc/settings/ConfigValidation.cpp
        ../src/render/d3d11/MonitorLayout.cpp
)
target_compile_features(alpharing_core_tests PRIVATE cxx_std_17)
target_include_directories(alpharing_core_tests PRIVATE ../src)

if (MSVC)
    target_compile_options(alpharing_core_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(alpharing_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME alpharing_core_tests COMMAND alpharing_core_tests)
