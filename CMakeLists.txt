cmake_minimum_required(VERSION 3.20)

project(itch_mold_replay)


include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SRCS src/*.cpp)
add_executable(itch_mold_replay ${SRCS})


option(DEBUG_NO_NETWORK "disable outbound traffic, server still processes file and retrans request\
		but does NOT call sendto(); to profile internal logic without network overhead")

if (DEBUG_NO_NETWORK)
	target_compile_definitions(itch_mold_replay PRIVATE DEBUG_NO_NETWORK)
endif ()

option(DEBUG_NO_SLEEP "disable sleep in downstream server; to profile / debug internal logic without sleeps")

if (DEBUG_NO_SLEEP)
	target_compile_definitions(itch_mold_replay PRIVATE DEBUG_NO_SLEEP)
endif ()


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




