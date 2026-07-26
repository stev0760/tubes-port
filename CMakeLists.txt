cmake_minimum_required(VERSION 3.16)
project(tubes-port CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(tubes-port
    src/main.cpp
    src/res.cpp
    src/gfx.cpp
    src/screen.cpp
)

target_include_directories(tubes-port PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(tubes-port PRIVATE ${SDL2_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tubes-port PRIVATE -Wall -Wextra)
endif()
