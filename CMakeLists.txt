cmake_minimum_required(VERSION 3.21)
project(AutoClicker VERSION 2.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
        version.h.in
        generated/version.h
        @ONLY)

add_executable(AutoClicker WIN32
        main.cpp
        app.rc)

target_include_directories(AutoClicker PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/generated>")

target_compile_definitions(AutoClicker PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN)

target_link_libraries(AutoClicker PRIVATE
        user32
        gdi32
        comctl32
        advapi32
        winmm)

if (MSVC)
    set_property(TARGET AutoClicker PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(AutoClicker PRIVATE /W4 /permissive-)
elseif (MINGW)
    target_compile_options(AutoClicker PRIVATE
            -Wall -Wextra -Wpedantic
            -ffunction-sections -fdata-sections)
    target_link_options(AutoClicker PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
            -Wl,--gc-sections
            -Wl,-u,__intrinsic_setjmpex
            -Wl,-u,__ms_vsnprintf)
    target_link_libraries(AutoClicker PRIVATE
            ucrt
            mingwex)
endif ()

install(TARGETS AutoClicker RUNTIME DESTINATION .)
