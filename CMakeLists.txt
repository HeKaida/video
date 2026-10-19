cmake_minimum_required(VERSION 3.10)
project(video C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "-Wall -Werror -O0 -g")

set(SOURCES
    main.c
    src/v4l2_capture.c
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(video ${SOURCES})
