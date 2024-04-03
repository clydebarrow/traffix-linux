cmake_minimum_required(VERSION 3.22)
project(traffix_linux C)

set(CMAKE_C_STANDARD 11)

add_executable(traffix_linux
        ./src/cJSON.c
        ./src/flarm.c
        ./src/gdl90.c
        ./src/gdltask.c
        ./src/main.c
        ./src/ownship.c
        ./src/riemann.c
        ./src/traffic.c
        ./src/udp.c
        )

add_subdirectory(websockets)

target_link_libraries(traffix_linux static_lib m)


target_include_directories(traffix_linux PUBLIC
        /opt/homebrew/Cellar/openssl@3/3.2.1/include/
        /usr/local/include/
        ./src)
