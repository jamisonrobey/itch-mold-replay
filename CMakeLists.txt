cmake_minimum_required(VERSION 3.16)

project(itch_mold_replay)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SRCS src/*.cpp)
add_executable(itch_mold_replay ${SRCS})

# my utils
add_subdirectory(external/jamutils)
target_include_directories(itch_mold_replay PRIVATE external/jamutils)

# CLI11
FetchContent_Declare(
        cli11_proj
        QUIET
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.5.0
)

set(CLI11_PRECOMPILED ON CACHE BOOL "Precompile CLI11")

FetchContent_MakeAvailable(cli11_proj)

target_link_libraries(itch_mold_replay PRIVATE
		CLI11::CLI11
		jamutils)


