file(GLOB src_file "*.cc")
set(exec_name 15_audio_mix)

include_directories(.)

add_executable(${exec_name} ${src_file})

target_link_libraries(${exec_name}
    avcodec
    avformat
    avutil
    swscale
    libswresample
)

