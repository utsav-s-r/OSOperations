cmake_minimum_required(VERSION 3.10)
project(ZenithOS C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(zenith_os main.c)

find_package(Curses REQUIRED)

include_directories(${CURSES_INCLUDE_DIR})
target_link_libraries(zenith_os ${CURSES_LIBRARIES})

if(WIN32)
    # Add any Windows specific compilation flags here if needed
else()
    target_compile_options(zenith_os PRIVATE -Wall -Wextra -O2)
    target_link_libraries(zenith_os "-framework Foundation" "-framework CoreFoundation" "-lobjc")
endif()
