cmake_minimum_required(VERSION 3.24)
project(gclone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "gclone is a native Windows desktop application")
endif()

add_executable(gclone WIN32
    src/main.cpp
    src/backend_process.cpp
    src/media_utils.cpp
    src/provisioner.cpp
)

target_include_directories(gclone PRIVATE src)
target_compile_definitions(gclone PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(gclone PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET gclone PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(gclone PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )
endif()

target_link_libraries(gclone PRIVATE
    comctl32
    comdlg32
    shell32
    winmm
    mfplat
    mfreadwrite
    mfuuid
    ole32
)

add_custom_command(TARGET gclone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/engines"
            "$<TARGET_FILE_DIR:gclone>/engines"
    COMMENT "Copying isolated engine launchers and workers"
)
