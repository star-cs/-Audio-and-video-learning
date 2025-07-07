#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FFmpeg库头文件
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>

// 音频输入缓冲区大小和重填充阈值
#define AUDIO_INBUF_SIZE 20480       // 输入缓冲区大小（20KB）
#define AUDIO_REFILL_THRESH 4096     // 当剩余数据小于此值时重新填充缓冲区

static char err_buf[128] = {0};      // 存储FFmpeg错误信息的缓冲区

/**
 * @brief 获取FFmpeg错误信息的可读字符串
 * @param errnum 错误码
 * @return 错误描述字符串
 */
static char* av_get_err(int errnum)
{
    av_strerror(errnum, err_buf, 128);
    return err_buf;
}

/**
 * @brief 打印音频帧的采样信息
 * @param frame 解码后的音频帧
 */
static void print_sample_format(const AVFrame *frame)
{
    printf("ar-samplerate: %uHz\n", frame->sample_rate);  // 采样率
    printf("ac-channel: %u\n", frame->channels);          // 声道数
    // 采样格式（注意：实际存储时已转为交错模式）
    printf("f-format: %u\n", frame->format);
}

/**
 * @brief 解码音频数据包
 * @param dec_ctx 解码器上下文
 * @param pkt 待解码的数据包
 * @param frame 存储解码后的音频帧
 * @param outfile 输出文件句柄
 */
static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
                   FILE *outfile)
{
    int i, ch;
    int ret, data_size;
    
    /* 将压缩数据包发送给解码器 */
    ret = avcodec_send_packet(dec_ctx, pkt);
    if(ret == AVERROR(EAGAIN)) {
        // API使用错误：应在接收帧后重新发送
        fprintf(stderr, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
    } else if (ret < 0) {
        // 提交数据包失败（非致命错误，继续处理）
        fprintf(stderr, "Error submitting the packet to the decoder, err:%s, pkt_size:%d\n",
                av_get_err(ret), pkt->size);
        return;
    }

    /* 读取所有输出帧（可能包含多个帧） */
    while (ret >= 0) {
        // 从解码器接收解码后的帧
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // 需要更多数据或已结束
            return;
        } else if (ret < 0) {
            // 解码过程中发生致命错误
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
        
        // 计算每个采样的字节大小
        data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
        if (data_size < 0) {
            fprintf(stderr, "Failed to calculate data size\n");
            exit(1);
        }
        
        // 首次解码时打印音频格式信息
        static int s_print_format = 0;
        if(s_print_format == 0) {
            s_print_format = 1;
            print_sample_format(frame);
        }
        
        /**
         * 平面格式(P)数据排列：LLLLLLRRRRRR（每个声道连续存储）
         * 交错格式数据排列：LRLRLRLR...（左右声道交替）
         * 注意：解码器输出可能是平面格式，这里转换为交错格式写入文件
         * 
         * 播放示例：ffplay -ar 48000 -ac 2 -f f32le output.pcm
         */
        for (i = 0; i < frame->nb_samples; i++) {
            for (ch = 0; ch < dec_ctx->channels; ch++) {
                // 将每个声道的每个采样写入文件（转换为交错格式）
                fwrite(frame->data[ch] + data_size * i, 1, data_size, outfile);
            }
        }
    }
}

// 播放范例：   ffplay -ar 48000 -ac 2 -f f32le believe.pcm
int main(int argc, char **argv)
{
    const char *outfilename;    // 输出文件名
    const char *filename;       // 输入文件名
    const AVCodec *codec;       // 解码器
    AVCodecContext *codec_ctx = NULL;  // 解码器上下文
    AVCodecParserContext *parser = NULL;  // 解析器上下文
    FILE *infile = NULL, *outfile = NULL;
    uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];  // 带填充的输入缓冲区
    uint8_t *data = NULL;       // 当前处理位置指针
    size_t data_size = 0;       // 当前有效数据大小
    AVPacket *pkt = NULL;       // 数据包
    AVFrame *decoded_frame = NULL;  // 解码后的帧

    // 参数检查
    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    outfilename = argv[2];

    // 根据文件扩展名确定解码器类型
    enum AVCodecID audio_codec_id;
    if (strstr(filename, "aac")) {
        audio_codec_id = AV_CODEC_ID_AAC;
    } else if (strstr(filename, "mp3")) {
        audio_codec_id = AV_CODEC_ID_MP3;
    } else {
        audio_codec_id = AV_CODEC_ID_AAC;  // 默认使用AAC
        printf("Using default codec id: %d\n", audio_codec_id);
    }

    // 分配数据包内存
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Failed to allocate packet\n");
        exit(1);
    }

    // 查找解码器
    codec = avcodec_find_decoder(audio_codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    // 初始化解析器
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

    // 打开输入/输出文件
    infile = fopen(filename, "rb");
    if (!infile) {
        perror("Could not open input file");
        exit(1);
    }
    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        perror("Could not open output file");
        exit(1);
    }

    // 首次读取数据
    data = inbuf;
    data_size = fread(inbuf, 1, AUDIO_INBUF_SIZE, infile);

    // 主解码循环
    while (data_size > 0) {
        // 延迟分配解码帧内存
        if (!decoded_frame) {
            decoded_frame = av_frame_alloc();
            if (!decoded_frame) {
                fprintf(stderr, "Could not allocate audio frame\n");
                exit(1);
            }
        }

        // 解析数据包
        int ret = av_parser_parse2(parser, codec_ctx, 
                                  &pkt->data, &pkt->size,  // 输出包数据/大小
                                  data, data_size,         // 输入数据
                                  AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            fprintf(stderr, "Error while parsing\n");
            exit(1);
        }

        // 更新数据指针和大小
        data += ret;
        data_size -= ret;

        // 如果解析出完整数据包，则进行解码
        if (pkt->size) {
            decode(codec_ctx, pkt, decoded_frame, outfile);
        }

        // 当数据不足时重新填充缓冲区
        if (data_size < AUDIO_REFILL_THRESH) {
            // 移动剩余数据到缓冲区头部
            memmove(inbuf, data, data_size);
            data = inbuf;
            // 读取新数据
            size_t len = fread(data + data_size, 1, 
                              AUDIO_INBUF_SIZE - data_size, infile);
            if (len > 0) {
                data_size += len;
            }
        }
    }

    /* 冲刷解码器（处理缓存中的数据） */
    pkt->data = NULL;   // 空包触发drain模式
    pkt->size = 0;
    decode(codec_ctx, pkt, decoded_frame, outfile);

    // 清理资源
    fclose(outfile);
    fclose(infile);
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    av_frame_free(&decoded_frame);
    av_packet_free(&pkt);

    printf("Decoding completed successfully.\n");
    printf("Play with: ffplay -ar <samplerate> -ac <channels> -f f32le %s\n", outfilename);
    return 0;
}