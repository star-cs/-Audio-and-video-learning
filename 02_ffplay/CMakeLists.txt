set(proj_name 02_ffplay)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${proj_name})

# # 查找本目录下所有文件
# file(GLOB SRC_FILES "*.c")

# foreach(src_file ${SRC_FILES})
#     get_filename_component(exec_name ${src_file} NAME_WE)
#     add_executable(${exec_name} ${src_file})

#     target_link_libraries(${exec_name}
#         avcodec
#         avformat
#         avutil
#         swscale
#         libswresample
#         avfilter
#     )
# endforeach()

add_subdirectory(ffplay_pro)