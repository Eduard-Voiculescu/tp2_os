cmake_minimum_required(VERSION 3.9)
project(tp2_os)

set(CMAKE_CXX_STANDARD 11)

add_executable(tp2_os
        .idea/codeStyles/Project.xml
        .idea/misc.xml
        .idea/modules.xml
        .idea/tp2_os.iml
        .idea/vcs.xml
        .idea/workspace.xml
        build/client/client_thread.o
        build/client/main.o
        build/server/main.o
        build/server/server_thread.o
        build/tp2_server
        client/client_thread.c
        client/client_thread.h
        client/main.c
        cmake-build-debug/cmake_install.cmake
        cmake-build-debug/CMakeCache.txt
        cmake-build-debug/Makefile
        cmake-build-debug/tp2_os.cbp
        server/main.c
        server/server_thread.c
        server/server_thread.h
        AUTHOR.md
        client.log
        GNUmakefile
        rapport.aux
        rapport.log
        rapport.pdf
        rapport.tex
        README.md
        tp2.tex)
