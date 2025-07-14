#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

int main(int argc, char *argv[]) // 修正参数类型为 char* argv[]
{
    int ret = 0;

    // 打开输入YUV文件
    FILE *inFile = NULL;
    const char *inFileName = "768x320.yuv";
    fopen_s(&inFile, inFileName, "rb+");
    if (!inFile) {
        printf("Fail to open file\n");
        return -1;
    }

    // 输入视频参数
    int in_width = 768;
    int in_height = 320;

    // 创建输出YUV文件
    FILE *outFile = NULL;
    const char *outFileName = "out_crop_vfilter.yuv";
    fopen_s(&outFile, outFileName, "wb");
    if (!outFile) {
        printf("Fail to create file for output\n");
        return -1;
    }

    // 注册所有滤镜
    avfilter_register_all();

    // 创建滤镜图
    AVFilterGraph *filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        printf("Fail to create filter graph!\n");
        return -1;
    }

    // 创建buffer source滤镜（输入源）
    char args[512];
    // 构造输入参数：视频尺寸、像素格式、时间基、宽高比
    sprintf(args, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", in_width,
            in_height, AV_PIX_FMT_YUV420P, 1, 25, 1, 1);

    const AVFilter *bufferSrc = avfilter_get_by_name("buffer");
    AVFilterContext *bufferSrc_ctx;
    ret = avfilter_graph_create_filter(&bufferSrc_ctx, bufferSrc, "in", args, NULL, filter_graph);
    if (ret < 0) {
        printf("Fail to create filter bufferSrc\n");
        return -1;
    }

    // 创建buffer sink滤镜（输出端）
    AVBufferSinkParams *bufferSink_params;
    AVFilterContext *bufferSink_ctx;
    const AVFilter *bufferSink = avfilter_get_by_name("buffersink");
    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE}; // 输出像素格式
    bufferSink_params = av_buffersink_params_alloc();
    bufferSink_params->pixel_fmts = pix_fmts;
    ret = avfilter_graph_create_filter(&bufferSink_ctx, bufferSink, "out", NULL, bufferSink_params,
                                       filter_graph);
    if (ret < 0) {
        printf("Fail to create filter sink filter\n");
        return -1;
    }

    // 创建split滤镜（分流）
    const AVFilter *splitFilter = avfilter_get_by_name("split");
    AVFilterContext *splitFilter_ctx;
    ret = avfilter_graph_create_filter(&splitFilter_ctx, splitFilter, "split",
                                       "outputs=2", // 分成两路输出
                                       NULL, filter_graph);
    if (ret < 0) {
        printf("Fail to create split filter\n");
        return -1;
    }

    // 创建crop滤镜（裁剪）
    const AVFilter *cropFilter = avfilter_get_by_name("crop");
    AVFilterContext *cropFilter_ctx;
    // 裁剪参数：输出宽=输入宽，输出高=输入高/2，位置(0,0)
    ret = avfilter_graph_create_filter(&cropFilter_ctx, cropFilter, "crop",
                                       "out_w=iw:out_h=ih/2:x=0:y=0", NULL, filter_graph);
    if (ret < 0) {
        printf("Fail to create crop filter\n");
        return -1;
    }

    // 创建vflip滤镜（垂直翻转）
    const AVFilter *vflipFilter = avfilter_get_by_name("vflip");
    AVFilterContext *vflipFilter_ctx;
    ret = avfilter_graph_create_filter(&vflipFilter_ctx, vflipFilter, "vflip", NULL, NULL,
                                       filter_graph);
    if (ret < 0) {
        printf("Fail to create vflip filter\n");
        return -1;
    }

    // 创建overlay滤镜（叠加）
    const AVFilter *overlayFilter = avfilter_get_by_name("overlay");
    AVFilterContext *overlayFilter_ctx;
    // 叠加参数：垂直位置为高度的一半（y=0:H/2）
    ret = avfilter_graph_create_filter(&overlayFilter_ctx, overlayFilter, "overlay", "y=0:H/2",
                                       NULL, filter_graph);
    if (ret < 0) {
        printf("Fail to create overlay filter\n");
        return -1;
    }

    // 连接滤镜：source -> split
    ret = avfilter_link(bufferSrc_ctx, 0, splitFilter_ctx, 0);
    if (ret != 0) {
        printf("Fail to link src filter and split filter\n");
        return -1;
    }
    // split主输出 -> overlay主输入
    ret = avfilter_link(splitFilter_ctx, 0, overlayFilter_ctx, 0);
    if (ret != 0) {
        printf("Fail to link split filter and overlay filter main pad\n");
        return -1;
    }
    // split第二输出 -> crop
    ret = avfilter_link(splitFilter_ctx, 1, cropFilter_ctx, 0);
    if (ret != 0) {
        printf("Fail to link split filter's second pad and crop filter\n");
        return -1;
    }
    // crop -> vflip
    ret = avfilter_link(cropFilter_ctx, 0, vflipFilter_ctx, 0);
    if (ret != 0) {
        printf("Fail to link crop filter and vflip filter\n");
        return -1;
    }
    // vflip -> overlay第二输入
    ret = avfilter_link(vflipFilter_ctx, 0, overlayFilter_ctx, 1);
    if (ret != 0) {
        printf("Fail to link vflip filter and overlay filter's second pad\n");
        return -1;
    }
    // overlay -> sink
    ret = avfilter_link(overlayFilter_ctx, 0, bufferSink_ctx, 0);
    if (ret != 0) {
        printf("Fail to link overlay filter and sink filter\n");
        return -1;
    }

    // 验证并配置滤镜图
    ret = avfilter_graph_config(filter_graph, NULL);
    if (ret < 0) {
        printf("Fail in filter graph\n");
        return -1;
    }

    // 打印滤镜图信息到文件
    char *graph_str = avfilter_graph_dump(filter_graph, NULL);
    FILE *graphFile = NULL;
    fopen_s(&graphFile, "graphFile.txt", "w"); // 打印filtergraph的具体情况
    fprintf(graphFile, "%s", graph_str);
    av_free(graph_str);

    // 分配输入帧
    AVFrame *frame_in = av_frame_alloc();
    size_t buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1);
    unsigned char *frame_buffer_in = (unsigned char *)av_malloc(buffer_size);
    av_image_fill_arrays(frame_in->data, frame_in->linesize, frame_buffer_in, AV_PIX_FMT_YUV420P,
                         in_width, in_height, 1);

    // 分配输出帧
    AVFrame *frame_out = av_frame_alloc();
    unsigned char *frame_buffer_out = (unsigned char *)av_malloc(buffer_size);
    av_image_fill_arrays(frame_out->data, frame_out->linesize, frame_buffer_out, AV_PIX_FMT_YUV420P,
                         in_width, in_height, 1);

    // 设置帧属性
    frame_in->width = in_width;
    frame_in->height = in_height;
    frame_in->format = AV_PIX_FMT_YUV420P;

    uint32_t frame_count = 0;
    while (1) {
        // 读取YUV数据（YUV420P格式大小为 w*h*3/2）
        size_t data_size = in_width * in_height * 3 / 2;
        if (fread(frame_buffer_in, 1, data_size, inFile) != data_size) {
            break; // 文件结束或读取错误
        }

        // 设置YUV平面指针
        frame_in->data[0] = frame_buffer_in;                                // Y分量
        frame_in->data[1] = frame_buffer_in + in_width * in_height;         // U分量
        frame_in->data[2] = frame_buffer_in + in_width * in_height * 5 / 4; // V分量

        // 将帧推入滤镜图
        if (av_buffersrc_add_frame(bufferSrc_ctx, frame_in) < 0) {
            printf("Error while add frame.\n");
            break;
        }

        // 从滤镜图获取处理后的帧
        ret = av_buffersink_get_frame(bufferSink_ctx, frame_out);
        if (ret < 0) {
            break; // 获取帧失败（可能是EOF）
        }

        // 写入处理后的YUV数据
        if (frame_out->format == AV_PIX_FMT_YUV420P) {
            // 写入Y分量（亮度）
            for (int i = 0; i < frame_out->height; i++) {
                fwrite(frame_out->data[0] + frame_out->linesize[0] * i, 1, frame_out->width,
                       outFile);
            }
            // 写入U分量（色度）
            for (int i = 0; i < frame_out->height / 2; i++) {
                fwrite(frame_out->data[1] + frame_out->linesize[1] * i, 1, frame_out->width / 2,
                       outFile);
            }
            // 写入V分量（色度）
            for (int i = 0; i < frame_out->height / 2; i++) {
                fwrite(frame_out->data[2] + frame_out->linesize[2] * i, 1, frame_out->width / 2,
                       outFile);
            }
        }

        // 更新帧计数器并打印进度
        ++frame_count;
        if (frame_count % 25 == 0) {
            printf("Process %d frame!\n", frame_count); // 每处理25帧输出一次进度
        }

        // 释放输出帧引用
        av_frame_unref(frame_out);
    }

    // 清理资源
    fclose(inFile);
    fclose(outFile);
    av_frame_free(&frame_in);
    av_frame_free(&frame_out);
    avfilter_graph_free(&filter_graph); // 内部会释放所有滤镜上下文

    return 0;
}