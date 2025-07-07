file(GLOB src_file "*.cpp")
set(exec_name 04_flv_parser_cplus)

include_directories(.)

add_executable(${exec_name} ${src_file})

# target_link_libraries(${exec_name}
#     avcodec
#     avformat
#     avutil
#     swscale
# )

