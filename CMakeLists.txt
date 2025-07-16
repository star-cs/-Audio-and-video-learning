cmake_minimum_required(VERSION 3.20)
project(AV_Learning C CXX)  # 支持 C 和 C++

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# FFmpeg路径
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-4.2.1-win32-dev")
include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)

# SDL2路径
set(SDL_ROOT "${CMAKE_SOURCE_DIR}/third_party/SDL2")
include_directories(${SDL_ROOT}/include)
link_directories(${SDL_ROOT}/lib/x86)


# 统一可执行程序输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 添加子目录
add_subdirectory(01_ffmpeg)
add_subdirectory(02_ffplay)