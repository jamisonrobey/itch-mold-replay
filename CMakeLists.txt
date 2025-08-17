cmake_minimum_required(VERSION 3.20)

project(itch-mold-replay)

include(FetchContent)

add_executable(itch-mold-replay 
    src/main.cpp
    src/server/message_buffer.cpp
    src/server/retransmission_server.cpp
    src/server/retransmission_worker.cpp
    src/server/downstream_server.cpp
)

if(DEBUG_NO_NETWORK)
    add_compile_definitions(DEBUG_NO_NETWORK)
endif()

if(DEBUG_NO_SLEEP)
    add_compile_definitions(DEBUG_NO_SLEEP)
endif()

add_subdirectory(external/jamutils)
target_include_directories(itch-mold-replay PRIVATE 
                           src/constants
                           src/server
                           external/jamutils)

FetchContent_Declare(
        cli11_proj
        QUIET
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.5.0
)

set(CLI11_PRECOMPILED ON CACHE BOOL "Precompile CLI11")

FetchContent_MakeAvailable(cli11_proj)

target_link_libraries(itch-mold-replay PRIVATE
		CLI11::CLI11
		jamutils)




