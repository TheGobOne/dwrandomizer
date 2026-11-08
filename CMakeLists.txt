# CMake build for the command-line randomizer. Checked on Windows with Ninja and GCC.

cmake_minimum_required(VERSION 3.7)
project(dwrandomizer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
endif()

#  file(GLOB music_files "${CMAKE_SOURCE_DIR}/expansion/music/*.txt")
#  foreach(music_file IN ITEMS ${music_files})
#      if(${music_file} IS_NEWER_THAN "${CMAKE_SOURCE_DIR}/common/credits_music.c")
#          message("${music_file} has changed, Rebuilding music file...")
#          execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/build_music.py")
#          break()
#      endif()
#  endforeach()
#  
#  file(GLOB asm_files "${CMAKE_SOURCE_DIR}/expansion/*.asm")
#  foreach(asm_file IN ITEMS ${asm_files})
#      if(${asm_file} IS_NEWER_THAN "${CMAKE_SOURCE_DIR}/common/expansion.c")
#          message("${asm_file} has changed, Rebuilding expansion file...")
#          execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/build_expansion.py")
#          break()
#      endif()
#  endforeach()

# Compile the command line version
include_directories(common)

set(SOURCE_FILES
        common/crc64.c
        common/sprites.c
        common/dwr.c
        common/map.c
        common/dungeon_maps.c
        common/stair_shuffle.c
        common/chests.c
        common/expansion.c
        common/mt19937-64.c
        common/flute_music.c
        common/chaos.c
        common/patch.c
        common/challenge.c
        common/polyfit.c
        common/base32.c
        common/credit_music.c
        common/custom_music.c)

if (ALPHA)
    add_definitions(-DDWR_ALPHA)
elseif(RELEASE)
    add_definitions(-DDWR_RELEASE)
endif()
if (CLEAR_UNUSED)
    add_definitions(-DCLEAR_ALL_UNUSED_DATA)
endif()

# emscripten build command (Archlinux)
#  cmake -DCMAKE_TOOLCHAIN_FILE=/usr/lib/emscripten/cmake/Modules/Platform/Emscripten.cmake ..
# emscripten build command (Ubuntu/Pop)
#  cmake -DCMAKE_TOOLCHAIN_FILE=/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake ..
if (EMSCRIPTEN)
    add_executable(dwrandomizer ${SOURCE_FILES})
    target_link_libraries(dwrandomizer m)
    target_link_options(dwrandomizer PRIVATE
        "SHELL:
        -s EXPORTED_FUNCTIONS='_dwr_randomize,_sprite_name,_version,_custom_music_load,_custom_music_clear'
        -s EXPORTED_RUNTIME_METHODS='ccall,cwrap,FS'
        -s FORCE_FILESYSTEM=1
        -s ALLOW_MEMORY_GROWTH=1
        -s WASM_BIGINT")
    file(REMOVE_RECURSE                                        ${CMAKE_BINARY_DIR}/release/)
    file(MAKE_DIRECTORY                                        ${CMAKE_BINARY_DIR}/release/)
    # The site is the generator. It needs the exports above, including FS.
    file(CREATE_LINK ${CMAKE_SOURCE_DIR}/site/index.html       ${CMAKE_BINARY_DIR}/release/index.html)
    file(COPY ${CMAKE_SOURCE_DIR}/site/sprites DESTINATION ${CMAKE_BINARY_DIR}/release)
    add_custom_command(TARGET dwrandomizer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/dwrandomizer.js ${CMAKE_BINARY_DIR}/release/)
    add_custom_command(TARGET dwrandomizer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/dwrandomizer.wasm ${CMAKE_BINARY_DIR}/release/)
else()
    add_executable(dwrandomizer cli/main.c ${SOURCE_FILES})
    target_link_libraries(dwrandomizer m)
endif()
