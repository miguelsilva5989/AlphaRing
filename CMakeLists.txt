cmake_minimum_required(VERSION 3.27)
project(alpha_ring)

set(CMAKE_CXX_STANDARD 17)

set(VERSION "1.3528.0.0")

set(WRAPPER_NAME "WTSAPI32")
set(WRAPPER_DLL_NAME "${WRAPPER_NAME}.dll")

if (MINGW)
    set(LIBRARY_LIST
            minhook imgui spdlog tinyxml2
            utils game
            version d3d11 dxgi d3dcompiler dwmapi ws2_32
    )
else()
    set(LIBRARY_LIST
            minhook imgui lua spdlog tinyxml2
            utils game
            Version
    )
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

# json
include_directories(${CMAKE_SOURCE_DIR}/lib/json/include)

if (MINGW)
    # Build dependency sources locally; the vendored .lib files are MSVC ABI archives.
    add_library(imgui STATIC
            ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp
            ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
            ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
            ${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
            ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_dx11.cpp
            ${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_win32.cpp
    )
    target_include_directories(imgui PUBLIC
            ${CMAKE_SOURCE_DIR}/third_party/imgui
            ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
    )

    add_library(spdlog INTERFACE)
    target_include_directories(spdlog INTERFACE ${CMAKE_SOURCE_DIR}/lib/spdlog/inc)

    add_library(tinyxml2 STATIC ${CMAKE_SOURCE_DIR}/third_party/tinyxml2/tinyxml2.cpp)
    target_include_directories(tinyxml2 PUBLIC ${CMAKE_SOURCE_DIR}/third_party/tinyxml2)

    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/minhook ${CMAKE_BINARY_DIR}/third_party/minhook EXCLUDE_FROM_ALL)

    include_directories(${CMAKE_SOURCE_DIR}/third_party/imgui)
    include_directories(${CMAKE_SOURCE_DIR}/third_party/imgui/backends)
    include_directories(${CMAKE_SOURCE_DIR}/third_party/tinyxml2)
    include_directories(${CMAKE_SOURCE_DIR}/third_party/minhook/include)
    include_directories(${CMAKE_SOURCE_DIR}/lib/lua/inc)
else()
    # imgui
    add_library(imgui SHARED IMPORTED)
    include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/inc)
    set_target_properties(imgui PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/imgui/lib/release/imgui.lib")

    # lua
    add_library(lua SHARED IMPORTED)
    include_directories(${CMAKE_SOURCE_DIR}/lib/lua/inc)
    set_target_properties(lua PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/lua/lib/release/lua.lib")

    # spdlog
    add_library(spdlog SHARED IMPORTED)
    include_directories(${CMAKE_SOURCE_DIR}/lib/spdlog/inc)
    set_target_properties(spdlog PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/spdlog/lib/release/spdlog.lib")

    # tinyxml2
    add_library(tinyxml2 SHARED IMPORTED)
    include_directories(${CMAKE_SOURCE_DIR}/lib/tinyxml2/inc)
    set_target_properties(tinyxml2 PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/tinyxml2/lib/release/tinyxml2.lib")

    # minhook
    add_library(minhook SHARED IMPORTED)
    include_directories(${CMAKE_SOURCE_DIR}/lib/minhook/inc)
    # set_target_properties(minhook PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/minhook/lib/${CMAKE_BUILD_TYPE}/libMinHook.x64.lib")
    set_target_properties(minhook PROPERTIES IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/lib/minhook/lib/release/libMinHook.x64.lib")
endif()

# utils
file(GLOB_RECURSE UTILS_SRC ${CMAKE_SOURCE_DIR}/lib/utils/src/*.cpp)
include_directories(${CMAKE_SOURCE_DIR}/lib/utils/inc)
include_directories(${CMAKE_SOURCE_DIR}/lib/utils/src)
add_library(utils ${UTILS_SRC} "lib/game/inc/1.3495.0.0/offset_groundhog.h" "lib/game/inc/1.3495.0.0/offset_halo1.h" "lib/game/inc/1.3495.0.0/offset_halo2.h" "lib/game/inc/1.3495.0.0/offset_halo3.h" "lib/game/inc/1.3495.0.0/offset_halo3odst.h" "lib/game/inc/1.3495.0.0/offset_halo4.h" "lib/game/inc/1.3495.0.0/offset_haloreach.h" "lib/game/inc/1.3495.0.0/offset_mcc.h")

# game
file(GLOB_RECURSE GAME_SRC ${CMAKE_SOURCE_DIR}/lib/game/src/*.cpp)
include_directories(${CMAKE_SOURCE_DIR}/lib/game/inc)
include_directories(${CMAKE_SOURCE_DIR}/lib/game/inc/${VERSION})
add_library(game ${GAME_SRC} "lib/game/inc/1.3495.0.0/offset_groundhog.h" "lib/game/inc/1.3495.0.0/offset_halo1.h" "lib/game/inc/1.3495.0.0/offset_halo2.h" "lib/game/inc/1.3495.0.0/offset_halo3.h" "lib/game/inc/1.3495.0.0/offset_halo3odst.h" "lib/game/inc/1.3495.0.0/offset_halo4.h" "lib/game/inc/1.3495.0.0/offset_haloreach.h" "lib/game/inc/1.3495.0.0/offset_mcc.h")

# disable warning c4996
add_definitions(-D_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS)
add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)

add_compile_definitions(WRAPPER_DLL_NAME="${WRAPPER_DLL_NAME}")
add_compile_definitions(GAME_VERSION="${VERSION}")

file(GLOB_RECURSE CORE_SRC ${CMAKE_SOURCE_DIR}/src/*.cpp)

if (MINGW)
    list(APPEND CORE_SRC ${CMAKE_SOURCE_DIR}/src/wrapper/WTSAPI32.def)
endif()

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
    target_link_options(${WRAPPER_NAME} PUBLIC /DEBUG)
elseif (MINGW)
    target_link_options(${WRAPPER_NAME} PUBLIC -g -static-libgcc -static-libstdc++ -static)
else ()
    target_link_options(${WRAPPER_NAME} PUBLIC -g)
endif()

install(TARGETS ${WRAPPER_NAME} DESTINATION "${MCC_DIR}/mcc/binaries/win64")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/res/" DESTINATION "${MCC_DIR}/alpha_ring")
