#include <stdio.h>
#include <libavutil/log.h>        // FFmpeg日志功能
#include <libavformat/avio.h>     // FFmpeg I/O操作
#include <libavformat/avformat.h> // FFmpeg格式处理

static char err_buf[128] = {0};
// 自定义错误信息获取函数
static char *av_get_err(int errnum)
{
    av_strerror(errnum, err_buf, 128); // 将错误码转换为可读字符串
    return err_buf;
}

/*
 * 功能：从MP4文件中提取H.264裸流并转换为Annex B格式
 * 参数：输入文件（MP4）、输出文件（.h264）
 * 原理：
 *   MP4中的H.264使用AVCC格式（NALU前加长度前缀），
 *   而标准H.264裸流需要Annex B格式（NALU以[00 00 00 01]分隔）
 * 比特流过滤器作用：
 *   1. 插入SPS/PPS参数集（解码必需头信息）
 *   2. 将长度前缀替换为起始码
 */
int main(int argc, char **argv)
{
    AVFormatContext *ifmt_ctx = NULL; // 输入文件上下文
    int videoindex = -1;              // 视频流索引
    AVPacket *pkt = NULL;             // 数据包
    int ret = -1;                     // 函数调用返回值
    int file_end = 0;                 // 文件结束标志

    // 参数检查
    if (argc < 3)
    {
        printf("Usage: %s <inputfile> <outfile>\n", argv[0]);
        return -1;
    }
    FILE *outfp = fopen(argv[2], "wb"); // 打开输出文件（H264裸流）
    printf("Input:%s Output:%s\n", argv[1], argv[2]);

    // ===== 1. 初始化FFmpeg上下文 =====
    ifmt_ctx = avformat_alloc_context(); // 分配输入上下文内存
    if (!ifmt_ctx)
    {
        printf("[Error] Could not allocate context.\n");
        return -1;
    }

    // ===== 2. 打开输入文件 =====
    ret = avformat_open_input(&ifmt_ctx, argv[1], NULL, NULL);
    if (ret != 0)
    {
        printf("[Error] avformat_open_input: %s\n", av_get_err(ret));
        return -1;
    }

    // ===== 3. 获取流信息 =====
    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (ret < 0)
    {
        printf("[Error] avformat_find_stream_info: %s\n", av_get_err(ret));
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // ===== 4. 查找视频流 =====
    videoindex = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (videoindex == -1)
    {
        printf("Didn't find a video stream.\n");
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // ===== 5. 初始化数据包 =====
    pkt = av_packet_alloc(); // 分配AVPacket内存
    av_init_packet(pkt);     // 初始化包字段

    // ===== 6. 配置比特流过滤器 =====
    // 关键作用：转换MP4格式H.264为Annex B格式
    const AVBitStreamFilter *bsfilter = av_bsf_get_by_name("h264_mp4toannexb");
    AVBSFContext *bsf_ctx = NULL;
    av_bsf_alloc(bsfilter, &bsf_ctx); // 创建过滤器上下文

    // 复制视频编解码参数到过滤器
    avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[videoindex]->codecpar);
    av_bsf_init(bsf_ctx); // 初始化过滤器

    // ===== 7. 数据处理主循环 =====
    file_end = 0;
    while (0 == file_end)
    {
        // 读取数据包
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0)
        {
            file_end = 1; // 设置文件结束标志
            printf("File end reached. Ret: %d\n", ret);
        }

        // 仅处理视频流
        if (ret == 0 && pkt->stream_index == videoindex)
        {
#if 1 // 被禁用的比特流处理路径（演示用）
      // 此路径使用过滤器处理MP4格式
            if (av_bsf_send_packet(bsf_ctx, pkt) != 0) {
                av_packet_unref(pkt);  
                continue;
            }
            av_packet_unref(pkt);
            while(av_bsf_receive_packet(bsf_ctx, pkt) == 0) {
                fwrite(pkt->data, 1, pkt->size, outfp); // 写入处理后的数据
                av_packet_unref(pkt);
            }
#else // 当前启用的直接写入路径（仅适用于TS流）
      // TS流已包含起始码，可直接写入
            size_t size = fwrite(pkt->data, 1, pkt->size, outfp);
            if (size != pkt->size)
            {
                printf("fwrite failed-> write:%u, pkt_size:%u\n", size, pkt->size);
            }
            av_packet_unref(pkt); // 释放包内存
#endif
        }
        else if (ret == 0)
        {
            av_packet_unref(pkt); // 非视频流释放包
        }
    }

    // ===== 8. 资源清理 =====
    if (outfp)
        fclose(outfp);
    if (bsf_ctx)
        av_bsf_free(&bsf_ctx);
    if (pkt)
        av_packet_free(&pkt);
    if (ifmt_ctx)
        avformat_close_input(&ifmt_ctx);

    printf("Extraction complete.\n");
    return 0;
}