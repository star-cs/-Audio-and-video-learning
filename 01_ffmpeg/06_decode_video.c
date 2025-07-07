#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>

#define VIDEO_INBUF_SIZE 20480      // 输入缓冲区大小
#define VIDEO_REFILL_THRESH 4096    // 当剩余数据低于此阈值时重新填充缓冲区

static char err_buf[128] = {0};     // 存储FFmpeg错误信息的缓冲区

// 将FFmpeg错误码转换为可读字符串
static char* av_get_err(int errnum)
{
    av_strerror(errnum, err_buf, 128);
    return err_buf;
}

// 打印视频帧的基本信息
static void print_video_format(const AVFrame *frame)
{
    printf("width: %u\n", frame->width);
    printf("height: %u\n", frame->height);
    printf("format: %u\n", frame->format); // 像素格式（如YUV420P）
}

/**
 * @brief 解码视频帧并将YUV数据写入文件
 * @param dec_ctx 解码器上下文
 * @param pkt 包含压缩数据的包
 * @param frame 存储解码后的帧
 * @param outfile 输出文件句柄
 */
static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile)
{
    int ret;
    
    // 发送压缩数据包给解码器
    ret = avcodec_send_packet(dec_ctx, pkt);
    if(ret == AVERROR(EAGAIN))
    {
        fprintf(stderr, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
    }
    else if (ret < 0)
    {
        fprintf(stderr, "Error submitting the packet to the decoder, err:%s, pkt_size:%d\n",
                av_get_err(ret), pkt->size);
        return;
    }

    // 循环获取所有解码完成的帧
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0)
        {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        
        // 首次解码成功时打印视频格式
        static int s_print_format = 0;
        if(s_print_format == 0)
        {
            s_print_format = 1;
            print_video_format(frame);
        }

        /**
         * 正确写入YUV420P数据的方法：
         * - Y平面：全尺寸，高度=frame->height
         * - U/V平面：1/4尺寸（宽高各减半）
         * 使用linesize处理行对齐问题（可能包含填充字节）
         */
        // 写入Y分量（亮度）
        for(int j=0; j<frame->height; j++)
            fwrite(frame->data[0] + j * frame->linesize[0], 1, frame->width, outfile);
        
        // 写入U分量（色度）
        for(int j=0; j<frame->height/2; j++)
            fwrite(frame->data[1] + j * frame->linesize[1], 1, frame->width/2, outfile);
        
        // 写入V分量（色度）
        for(int j=0; j<frame->height/2; j++)
            fwrite(frame->data[2] + j * frame->linesize[2], 1, frame->width/2, outfile);
    }
}

/**
 * 使用示例：
 * 提取H264: ffmpeg -i input.flv -vcodec libx264 -an -f h264 output.h264
 * 提取MPEG2: ffmpeg -i input.flv -vcodec mpeg2video -an -f mpeg2video output.mpeg2
 * 播放YUV: ffplay -pixel_format yuv420p -video_size 768x320 -framerate 25 output.yuv
 */
int main(int argc, char **argv)
{
    const char *outfilename;    // 输出YUV文件名
    const char *filename;       // 输入视频文件（H.264/MPEG2）
    const AVCodec *codec;       // 解码器
    AVCodecContext *codec_ctx= NULL; // 解码器上下文
    AVCodecParserContext *parser = NULL; // 解析器上下文
    FILE *infile = NULL, *outfile = NULL;
    uint8_t inbuf[VIDEO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE]; // 输入缓冲区（带填充）
    uint8_t *data = NULL;       // 当前处理的数据指针
    size_t   data_size = 0;     // 当前缓冲区有效数据大小
    AVPacket *pkt = NULL;       // 存储压缩数据包
    AVFrame *decoded_frame = NULL; // 存储解码帧
    int len = 0;
    int ret = 0;
    
    if (argc <= 2)
    {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];  // 输入文件路径
    outfilename = argv[2];  // 输出文件路径

    pkt = av_packet_alloc(); // 分配AVPacket
    
    // 根据文件扩展名确定解码器类型
    enum AVCodecID video_codec_id = AV_CODEC_ID_H264;
    if(strstr(filename, "264") != NULL) {
        video_codec_id = AV_CODEC_ID_H264;
    }
    else if(strstr(filename, "mpeg2") != NULL) {
        video_codec_id = AV_CODEC_ID_MPEG2VIDEO;
    }
    else {
        printf("default codec id:%d\n", video_codec_id);
    }

    // 查找解码器
    codec = avcodec_find_decoder(video_codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }
    
    // 初始化解析器（用于分割裸流为NALU）
    parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "Parser not found\n");
        exit(1);
    }
    
    // 创建解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    // 打开输入文件（视频裸流）
    infile = fopen(filename, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }
    
    // 打开输出文件（YUV原始数据）
    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        av_free(codec_ctx);
        exit(1);
    }

    // 初始化数据指针和大小
    data = inbuf;
    data_size = fread(inbuf, 1, VIDEO_INBUF_SIZE, infile);

    // 主解码循环
    while (data_size > 0)
    {
        // 首次进入时分配帧内存
        if (!decoded_frame) {
            if (!(decoded_frame = av_frame_alloc())) {
                fprintf(stderr, "Could not allocate audio frame\n");
                exit(1);
            }
        }

        // 使用解析器分割数据包
        ret = av_parser_parse2(parser, codec_ctx, &pkt->data, &pkt->size,
                               data, data_size,
                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            fprintf(stderr, "Error while parsing\n");
            exit(1);
        }
        
        // 移动数据指针
        data      += ret;   // 跳过已解析数据
        data_size -= ret;   // 更新剩余数据大小

        // 如果解析出完整数据包，进行解码
        if (pkt->size) {
            decode(codec_ctx, pkt, decoded_frame, outfile);
        }

        // 当剩余数据不足时，重新填充缓冲区
        if (data_size < VIDEO_REFILL_THRESH) {
            // 将剩余数据移到缓冲区头部
            memmove(inbuf, data, data_size);
            data = inbuf;
            // 读取新数据到缓冲区剩余空间
            len = fread(data + data_size, 1, VIDEO_INBUF_SIZE - data_size, infile);
            if (len > 0) {
                data_size += len;
            }
        }
    }

    /* 冲刷解码器（处理缓存帧） */
    pkt->data = NULL;   // 发送空包触发drain mode
    pkt->size = 0;
    decode(codec_ctx, pkt, decoded_frame, outfile);

    // 清理资源
    fclose(outfile);
    fclose(infile);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    av_frame_free(&decoded_frame);
    av_packet_free(&pkt);

    printf("Decoding completed successfully\n");
    return 0;
}