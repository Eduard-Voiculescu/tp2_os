cmake_minimum_required(VERSION 3.9)
project(tp2_os)

set(CMAKE_CXX_STANDARD 11)

include_directories(.)
include_directories(client)
include_directories(server)

add_executable(tp2_os
        client/client_thread.c
        client/client_thread.h
        client/main.c
        server/main.c
        server/server_thread.c
        server/server_thread.h
        AUTHOR.md
        GNUmakefile
        rapport.tex
        README.md
        tp2.tex)
