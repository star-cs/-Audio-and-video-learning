file(GLOB src_file "*.c")
set(exec_name 09_02_audio_resample)

include_directories(.)

add_executable(${exec_name} ${src_file})

target_link_libraries(${exec_name}
    avcodec
    avformat
    avutil
    swscale
    libswresample
)

