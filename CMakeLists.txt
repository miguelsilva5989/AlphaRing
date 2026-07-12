cmake_minimum_required(VERSION 3.27)
project(alpha_ring LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

set(VERSION "1.3528.0.0")
set(ALPHARING_VERSION "1.5.0")
set(ALPHARING_VERSION_COMMA "1,5,0,0")

execute_process(
        COMMAND git rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ALPHARING_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
)
if (NOT ALPHARING_GIT_SHA)
    set(ALPHARING_GIT_SHA "unknown")
endif()

option(ALPHARING_DEVTOOLS "Build unsafe/internal developer tools" OFF)
option(ALPHARING_WARNINGS_AS_ERRORS "Treat AlphaRing warnings as errors" OFF)

set(WRAPPER_NAME "WTSAPI32")
set(WRAPPER_DLL_NAME "${WRAPPER_NAME}.dll")

set(LIBRARY_LIST
        minhook imgui spdlog tinyxml2
        utils game
        version d3d11 dxgi d3dcompiler dwmapi ws2_32
)

include_directories(${CMAKE_SOURCE_DIR}/src)

# A source checkout must provide these pinned submodules on every toolchain.
set(ALPHARING_REQUIRED_SUBMODULE_FILES
        third_party/imgui/imgui.cpp
        third_party/minhook/CMakeLists.txt
        third_party/tinyxml2/tinyxml2.cpp
)
foreach(dependency_file IN LISTS ALPHARING_REQUIRED_SUBMODULE_FILES)
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/${dependency_file}")
        message(FATAL_ERROR
                "Missing ${dependency_file}. Run: git submodule update --init --recursive")
    endif()
endforeach()

add_library(imgui STATIC
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_dx11.cpp
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party/imgui
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
)

add_library(spdlog INTERFACE)
target_include_directories(spdlog SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/lib/spdlog/inc)
target_compile_definitions(spdlog INTERFACE SPDLOG_HEADER_ONLY)

add_library(tinyxml2 STATIC ${CMAKE_SOURCE_DIR}/third_party/tinyxml2/tinyxml2.cpp)
target_include_directories(tinyxml2 SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/tinyxml2)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/minhook ${CMAKE_BINARY_DIR}/third_party/minhook EXCLUDE_FROM_ALL)

# json
include_directories(${CMAKE_SOURCE_DIR}/lib/json/include)

# utils
file(GLOB_RECURSE UTILS_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/utils/src/*.cpp)
include_directories(${CMAKE_SOURCE_DIR}/lib/utils/inc)
add_library(utils ${UTILS_SRC} "lib/game/inc/1.3495.0.0/offset_groundhog.h" "lib/game/inc/1.3495.0.0/offset_halo1.h" "lib/game/inc/1.3495.0.0/offset_halo2.h" "lib/game/inc/1.3495.0.0/offset_halo3.h" "lib/game/inc/1.3495.0.0/offset_halo3odst.h" "lib/game/inc/1.3495.0.0/offset_halo4.h" "lib/game/inc/1.3495.0.0/offset_haloreach.h" "lib/game/inc/1.3495.0.0/offset_mcc.h")

# game
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lib/game/src/*.cpp)
include_directories(${CMAKE_SOURCE_DIR}/lib/game/inc)
include_directories(${CMAKE_SOURCE_DIR}/lib/game/inc/${VERSION})
add_library(game ${GAME_SRC} "lib/game/inc/1.3495.0.0/offset_groundhog.h" "lib/game/inc/1.3495.0.0/offset_halo1.h" "lib/game/inc/1.3495.0.0/offset_halo2.h" "lib/game/inc/1.3495.0.0/offset_halo3.h" "lib/game/inc/1.3495.0.0/offset_halo3odst.h" "lib/game/inc/1.3495.0.0/offset_halo4.h" "lib/game/inc/1.3495.0.0/offset_haloreach.h" "lib/game/inc/1.3495.0.0/offset_mcc.h")

# disable warning c4996
add_definitions(-D_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS)
add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)

add_compile_definitions(WRAPPER_DLL_NAME="${WRAPPER_DLL_NAME}")
add_compile_definitions(GAME_VERSION="${VERSION}")
add_compile_definitions(ALPHARING_DEVTOOLS=$<BOOL:${ALPHARING_DEVTOOLS}>)
add_compile_definitions(ALPHARING_VERSION="${ALPHARING_VERSION}")
add_compile_definitions(ALPHARING_BUILD_SHA="${ALPHARING_GIT_SHA}")

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

if (NOT ALPHARING_DEVTOOLS)
    list(FILTER CORE_SRC EXCLUDE REGEX "/mcc/network/Network\\.cpp$")
endif()

if (MINGW)
    list(APPEND CORE_SRC ${CMAKE_SOURCE_DIR}/src/wrapper/WTSAPI32.def)
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
configure_file(
        ${CMAKE_SOURCE_DIR}/src/wrapper/version.rc.in
        ${CMAKE_BINARY_DIR}/generated/version.rc
        @ONLY
)
list(APPEND CORE_SRC ${CMAKE_BINARY_DIR}/generated/version.rc)

add_library(${WRAPPER_NAME} SHARED ${CORE_SRC} "lib/game/inc/1.3495.0.0/offset_groundhog.h" "lib/game/inc/1.3495.0.0/offset_halo1.h" "lib/game/inc/1.3495.0.0/offset_halo2.h" "lib/game/inc/1.3495.0.0/offset_halo3.h" "lib/game/inc/1.3495.0.0/offset_halo3odst.h" "lib/game/inc/1.3495.0.0/offset_halo4.h" "lib/game/inc/1.3495.0.0/offset_haloreach.h" "lib/game/inc/1.3495.0.0/offset_mcc.h")

if (MINGW)
    set_target_properties(${WRAPPER_NAME} PROPERTIES PREFIX "")
endif()

if (MINGW)
    foreach(target utils game ${WRAPPER_NAME})
        target_compile_options(${target} PRIVATE -include "${CMAKE_SOURCE_DIR}/src/mingw_compat.h")
    endforeach()
endif()

target_link_libraries(${WRAPPER_NAME} PUBLIC ${LIBRARY_LIST})

if (MSVC)
    target_compile_options(${WRAPPER_NAME} PRIVATE /W4 /permissive-)
    target_compile_options(${WRAPPER_NAME} PRIVATE $<$<CONFIG:Release>:/Zi>)
    if (ALPHARING_WARNINGS_AS_ERRORS)
        target_compile_options(${WRAPPER_NAME} PRIVATE /WX)
    endif()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${WRAPPER_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    if (ALPHARING_WARNINGS_AS_ERRORS)
        target_compile_options(${WRAPPER_NAME} PRIVATE -Werror)
    endif()
endif()

if (MSVC)
    target_link_options(${WRAPPER_NAME} PRIVATE /DEBUG)
elseif (MINGW)
    target_compile_options(${WRAPPER_NAME} PRIVATE $<$<CONFIG:Release>:-g>)
    target_link_options(${WRAPPER_NAME} PRIVATE -static-libgcc -static-libstdc++ -static)

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        find_program(ALPHARING_OBJCOPY x86_64-w64-mingw32-objcopy REQUIRED)
        add_custom_command(TARGET ${WRAPPER_NAME} POST_BUILD
                COMMAND ${ALPHARING_OBJCOPY} --only-keep-debug
                        $<TARGET_FILE:${WRAPPER_NAME}> $<TARGET_FILE:${WRAPPER_NAME}>.debug
                COMMAND ${ALPHARING_OBJCOPY} --strip-debug $<TARGET_FILE:${WRAPPER_NAME}>
                COMMAND ${ALPHARING_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:${WRAPPER_NAME}>.debug
                        $<TARGET_FILE:${WRAPPER_NAME}>
                VERBATIM
        )
    endif()
endif()

install(TARGETS ${WRAPPER_NAME} DESTINATION "${MCC_DIR}/mcc/binaries/win64")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/" DESTINATION "${MCC_DIR}/alpha_ring")
