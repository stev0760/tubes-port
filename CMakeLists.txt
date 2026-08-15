cmake_minimum_required(VERSION 3.16)

# One source of truth: src/version.h. CMake reads the numbers out of it rather
# than carrying a second copy that can drift from the one the program prints.
# The FATAL_ERROR is the point of the loop - without it, renaming a macro would
# yield project(VERSION ..) with empty fields and no complaint at all.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" _tubes_version_h)
foreach(_part MAJOR MINOR PATCH)
    if(NOT _tubes_version_h MATCHES "#define TUBES_VERSION_${_part} ([0-9]+)")
        message(FATAL_ERROR "src/version.h has no TUBES_VERSION_${_part}")
    endif()
    set(_tubes_${_part} "${CMAKE_MATCH_1}")
endforeach()

project(tubes-port VERSION ${_tubes_MAJOR}.${_tubes_MINOR}.${_tubes_PATCH}
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

find_package(SDL2 REQUIRED)

# Nuked-OPL3, used unmodified. Emulating the chip is a solved problem and not
# what this project reverse engineers; see third_party/nuked-opl3/LICENSE.
add_library(nuked-opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(nuked-opl3 PUBLIC third_party/nuked-opl3)

add_executable(tubes-port
    src/main.cpp
    src/screens.cpp
    src/uistate.cpp
    src/present.cpp
    src/boot.cpp
    src/res.cpp
    src/gfx.cpp
    src/screen.cpp
    src/font.cpp
    src/sfx.cpp
    src/scr.cpp
    src/board.cpp
    src/menu.cpp
    src/session.cpp
    src/hiscore.cpp
    src/save.cpp
    src/input.cpp
    src/instructions.cpp
    src/cutscene.cpp
    src/ending.cpp
    src/credits.cpp
    src/wave.cpp
    src/wave_text.cpp
    src/game.cpp
    src/ordering.cpp
    src/registration.cpp
    src/textscreen.cpp
    src/cp437_font.cpp
    src/mus.cpp
    src/opl.cpp
    src/playerfiles.cpp
)

target_include_directories(tubes-port PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(tubes-port PRIVATE nuked-opl3 ${SDL2_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tubes-port PRIVATE -Wall -Wextra)
endif()

add_executable(tubes-tests src/board_test.cpp src/board.cpp src/menu.cpp src/session.cpp src/hiscore.cpp src/save.cpp src/input.cpp src/instructions.cpp src/cutscene.cpp src/ending.cpp src/credits.cpp src/wave.cpp src/wave_text.cpp src/game.cpp
                            src/font.cpp src/screen.cpp src/gfx.cpp
                            src/ordering.cpp
    src/registration.cpp
    src/textscreen.cpp src/cp437_font.cpp
    src/screens.cpp src/uistate.cpp src/res.cpp src/playerfiles.cpp
                            src/sfx.cpp src/scr.cpp)
target_include_directories(tubes-tests PRIVATE src)
enable_testing()
add_test(NAME board COMMAND tubes-tests)
