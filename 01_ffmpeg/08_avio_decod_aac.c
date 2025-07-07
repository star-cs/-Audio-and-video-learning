#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define BUF_SIZE 20480  // 自定义IO缓冲区大小

// 获取FFmpeg错误码对应的可读字符串
static char* av_get_err(int errnum)
{
    static char err_buf[128] = {0};
    av_strerror(errnum, err_buf, 128);
    return err_buf;
}

// 打印音频帧的基本信息
static void print_sample_format(const AVFrame *frame)
{
    printf("ar-samplerate: %uHz\n", frame->sample_rate);  // 采样率
    printf("ac-channel: %u\n", frame->channels);          // 声道数
    printf("f-format: %u\n", frame->format);             // 采样格式（注意实际存储时可能已转成交错模式）
}

// 自定义IO读取函数（供AVIOContext使用）
static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    FILE *in_file = (FILE *)opaque;
    int read_size = fread(buf, 1, buf_size, in_file);
    if(read_size <=0) {
        return AVERROR_EOF;     // 文件结束标志
    }
    return read_size;
}

// 音频解码核心函数
static void decode(AVCodecContext *dec_ctx, AVPacket *packet, AVFrame *frame, FILE *outfile)
{
    int ret = 0;
    // 发送压缩数据包给解码器
    ret = avcodec_send_packet(dec_ctx, packet);
    if(ret == AVERROR(EAGAIN)) {
        printf("Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
    } else if(ret < 0) {
        printf("Error submitting the packet to the decoder, err:%s\n", av_get_err(ret));
        return;
    }

    while (ret >= 0) {
        // 从解码器获取解码后的音频帧
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;  // 需要更多数据或解码结束
        } else if (ret < 0)  {
            printf("Error during decoding\n");
            exit(1);
        }

        // 计算每个样本的字节大小
        int data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
        
        /* 处理Planar音频格式（平面存储）：
         * Planar格式存储：LLLLLLRRRRRR（每个声道数据连续存储）
         * 交错格式存储：LRLRLRLR...（左右声道数据交替存储）
         * 本函数将Planar格式转换为交错格式写入PCM文件
         */
        for(int i = 0; i < frame->nb_samples; i++) {          // 遍历所有样本
            for(int ch = 0; ch < dec_ctx->channels; ch++) {   // 遍历所有声道
                // 将单个样本写入文件（实现Planar到交错的转换）
                fwrite(frame->data[ch] + data_size * i, 1, data_size, outfile);
            }
        }
    }
}

// 播放范例：   ffplay -ar 48000 -ac 2 -f f32le believe.pcm

int main(int argc, char **argv)
{
    if(argc != 3) {
        printf("usage: %s <intput file> <out file>\n", argv[0]);
        return -1;
    }
    const char *in_file_name = argv[1];  // 输入文件（AAC/MP3等压缩音频）
    const char *out_file_name = argv[2]; // 输出文件（原始PCM数据）
    FILE *in_file = NULL;
    FILE *out_file = NULL;

    // 1. 打开输入/输出文件
    in_file = fopen(in_file_name, "rb");
    if(!in_file) {
        printf("open file %s failed\n", in_file_name);
        return  -1;
    }
    out_file = fopen(out_file_name, "wb");
    if(!out_file) {
        printf("open file %s failed\n", out_file_name);
        return  -1;
    }

    // 2. 设置自定义IO上下文（用于从文件流读取）
    uint8_t *io_buffer = av_malloc(BUF_SIZE);  // 分配IO缓冲区
    AVIOContext *avio_ctx = avio_alloc_context(
        io_buffer,    // 内部缓冲区
        BUF_SIZE,     // 缓冲区大小
        0,            // 非写模式
        (void *)in_file,  // 传递给回调函数的opaque指针
        read_packet,  // 自定义读取函数
        NULL,         // 无写函数
        NULL          // 无seek函数
    );

    // 3. 创建并配置格式上下文
    AVFormatContext *format_ctx = avformat_alloc_context();
    format_ctx->pb = avio_ctx;  // 绑定自定义IO
    int ret = avformat_open_input(&format_ctx, NULL, NULL, NULL);
    if(ret < 0) {
        printf("avformat_open_input failed:%s\n", av_err2str(ret));
        return -1;
    }

    // 4. 初始化解码器（这里硬编码为AAC解码器）
    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if(!codec) {
        printf("avcodec_find_decoder failed\n");
        return -1;
    }

    // 5. 创建解码器上下文
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if(!codec_ctx) {
        printf("avcodec_alloc_context3 failed\n");
        return -1;
    }
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if(ret < 0) {
        printf("avcodec_open2 failed:%s\n", av_err2str(ret));
        return -1;
    }

    // 6. 分配数据包和帧结构
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // 7. 主解码循环
    while (1) {
        ret = av_read_frame(format_ctx, packet);  // 读取压缩数据包
        if(ret < 0) {
            printf("av_read_frame failed:%s\n", av_err2str(ret));
            break;
        }
        decode(codec_ctx, packet, frame, out_file);  // 解码数据包
    }

    // 8. 刷新解码器缓冲区（处理剩余帧）
    printf("read file finish\n");
    decode(codec_ctx, NULL, frame, out_file);  // 传入NULL刷新解码器

    // 9. 清理资源
    fclose(in_file);
    fclose(out_file);

    av_free(io_buffer);  // 释放IO缓冲区
    av_frame_free(&frame);
    av_packet_free(&packet);

    avformat_close_input(&format_ctx);
    avcodec_free_context(&codec_ctx);

    printf("main finish\n");
    return 0;
}