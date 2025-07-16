file(GLOB src_file "*.c")
set(exec_name 02_ffplay_pro)

include_directories(.)

add_executable(${exec_name} ${src_file})

target_link_libraries(${exec_name}
    avcodec
    avformat
    avutil
    swscale
    swresample
    avdevice
    avfilter
    postproc
    SDL2
)