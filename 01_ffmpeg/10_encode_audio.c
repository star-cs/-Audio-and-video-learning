/**
 * @brief         音频编码示例程序
 *               从本地读取PCM数据进行AAC编码
 *               关键点说明：
 *               1. 输入PCM格式必须匹配编码器要求
 *                 - 默认aac编码器要求AV_SAMPLE_FMT_FLTP格式
 *                 - libfdk_aac编码器要求AV_SAMPLE_FMT_S16格式
 *               2. 通过编码器参数检查支持的采样率和格式
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // 添加了string.h头文件用于memset等操作

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>

/**
  * 检查编码器是否支持指定的采样格式
  * @param codec 目标编码器
  * @param sample_fmt 要检查的采样格式
  * @return 1表示支持，0表示不支持
  */
static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

/**
  * 检查编码器是否支持指定的采样率
  * @param codec 目标编码器
  * @param sample_rate 要检查的采样率
  * @return 1表示支持，0表示不支持
  */
static int check_sample_rate(const AVCodec *codec, const int sample_rate)
{
    const int *p = codec->supported_samplerates;
    while (*p != 0) {
        printf("%s support %dhz\n", codec->name, *p); // 保留原始日志
        if (*p == sample_rate)
            return 1;
        p++;
    }
    return 0;
}

/**
  * 检查编码器是否支持指定的声道布局
  * @param codec 目标编码器
  * @param channel_layout 要检查的声道布局
  * @return 1表示支持，0表示不支持
  */
static int check_channel_layout(const AVCodec *codec, const uint64_t channel_layout)
{
    const uint64_t *p = codec->channel_layouts;
    if (!p) {
        printf("the codec %s no set channel_layouts\n", codec->name); // 保留原始日志
        return 1; // 未设置声道布局时默认支持
    }
    while (*p != 0) {
        printf("%s support channel_layout %llu\n", codec->name, *p); // 保留原始日志
        if (*p == channel_layout)
            return 1;
        p++;
    }
    return 0;
}

/**
  * 生成AAC ADTS头
  * @param ctx 编码器上下文
  * @param adts_header 用于存储生成的ADTS头(7字节)
  * @param aac_length AAC数据长度(不含ADTS头)
  */
static void get_adts_header(AVCodecContext *ctx, uint8_t *adts_header, int aac_length)
{
    uint8_t freq_idx = 0;
    // 采样率映射表
    switch (ctx->sample_rate) {
        case 96000:
            freq_idx = 0;
            break;
        case 88200:
            freq_idx = 1;
            break;
        case 64000:
            freq_idx = 2;
            break;
        case 48000:
            freq_idx = 3;
            break;
        case 44100:
            freq_idx = 4;
            break;
        case 32000:
            freq_idx = 5;
            break;
        case 24000:
            freq_idx = 6;
            break;
        case 22050:
            freq_idx = 7;
            break;
        case 16000:
            freq_idx = 8;
            break;
        case 12000:
            freq_idx = 9;
            break;
        case 11025:
            freq_idx = 10;
            break;
        case 8000:
            freq_idx = 11;
            break;
        case 7350:
            freq_idx = 12;
            break;
        default:
            freq_idx = 4;
            break; // 默认44100Hz
    }

    uint8_t chanCfg = ctx->channels;
    uint32_t frame_length = aac_length + 7; // ADTS头长度固定为7字节

    // ADTS头结构构建
    adts_header[0] = 0xFF; // 同步字节1
    adts_header[1] = 0xF1; // 同步字节2 + 版本等
    adts_header[2] = ((ctx->profile) << 6) + (freq_idx << 2) + (chanCfg >> 2);
    adts_header[3] = (((chanCfg & 3) << 6) + (frame_length >> 11));
    adts_header[4] = ((frame_length & 0x7FF) >> 3);
    adts_header[5] = (((frame_length & 7) << 5) + 0x1F);
    adts_header[6] = 0xFC; // CRC校验配置
}

/**
  * 音频编码核心函数
  * @param ctx 编码器上下文
  * @param frame 待编码的音频帧(传NULL表示刷新编码器)
  * @param pkt 输出编码后的数据包
  * @param output 输出文件指针
  * @return 0成功，负数表示错误
  */
static int encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, FILE *output)
{
    int ret;

    // 发送音频帧到编码器
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        return -1;
    }

    // 循环接收所有可用的编码数据包
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0; // 需要更多输入或已结束
        } else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            return -1;
        }

        size_t len = 0;
        // 检查是否需要添加ADTS头
        printf("ctx->flags:0x%x & AV_CODEC_FLAG_GLOBAL_HEADER:0x%x, name:%s\n", ctx->flags,
               ctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER, ctx->codec->name);

        if ((ctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
            // 生成并写入ADTS头
            uint8_t aac_header[7];
            get_adts_header(ctx, aac_header, pkt->size);
            len = fwrite(aac_header, 1, 7, output);
            if (len != 7) {
                fprintf(stderr, "fwrite aac_header failed\n");
                return -1;
            }
        }

        // 写入编码后的AAC数据
        len = fwrite(pkt->data, 1, pkt->size, output);
        if (len != pkt->size) {
            fprintf(stderr, "fwrite aac data failed\n");
            return -1;
        }

        // 注意：不需要手动释放pkt，avcodec_receive_packet内部会处理
        // 不能将pkt直接插入到队列，因为编码器会释放数据
        // 可以新分配一个pkt, 然后使用av_packet_move_ref转移pkt对应的buffer
    }
    return -1;
}

/**
  * 将交错的32位浮点PCM数据（f32le，packed）转换为平面的32位浮点格式（fltp，planar）
  * @param f32le 输入PCM数据(交错格式)
  * @param fltp 输出PCM数据(平面格式)
  * @param nb_samples 单通道采样点数
  */
void f32le_convert_to_fltp(float *f32le, float *fltp, int nb_samples)
{
    float *fltp_l = fltp;              // 左声道数据起始位置
    float *fltp_r = fltp + nb_samples; // 右声道数据起始位置

    // 分离交错数据到平面格式
    for (int i = 0; i < nb_samples; i++) {
        fltp_l[i] = f32le[i * 2];     // 左声道
        fltp_r[i] = f32le[i * 2 + 1]; // 右声道
    }
}
/*
 * 提取测试文件：
 * （1）s16格式：ffmpeg -i buweishui.aac -ar 48000 -ac 2 -f s16le 48000_2_s16le.pcm
 * （2）flt格式：ffmpeg -i buweishui.aac -ar 48000 -ac 2 -f f32le 48000_2_f32le.pcm
 *      ffmpeg只能提取packed格式的PCM数据，在编码时候如果输入要为fltp则需要进行转换
 * 测试范例:
 * （1）48000_2_s16le.pcm libfdk_aac.aac libfdk_aac  // 如果编译的时候没有支持fdk aac则提示找不到编码器
 * （2）48000_2_f32le.pcm aac.aac aac // 我们这里只测试aac编码器，不测试fdkaac
*/
int main(int argc, char **argv)
{
    // 参数解析
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_file out_file [codec_name]>, argc:%d\n", argv[0], argc);
        return 0;
    }
    char *in_pcm_file = argv[1];  // 输入PCM文件路径
    char *out_aac_file = argv[2]; // 输出AAC文件路径
    char *codec_name = NULL;
    int force_codec = 0; // 是否强制使用指定编码器

    // 处理可选编码器参数
    if (4 == argc) {
        if (strcmp(argv[3], "libfdk_aac") == 0) {
            force_codec = 1;
            codec_name = "libfdk_aac";
        } else if (strcmp(argv[3], "aac") == 0) {
            force_codec = 1;
            codec_name = "aac";
        }
    }

    // 打印编码器选择信息
    if (force_codec)
        printf("force codec name: %s\n", codec_name);
    else
        printf("default codec name: %s\n", "aac");

    // 查找编码器
    const AVCodec *codec = NULL;
    if (force_codec == 0) {
        codec = avcodec_find_encoder(AV_CODEC_ID_AAC); // 默认查找AAC编码器
    } else {
        codec = avcodec_find_encoder_by_name(codec_name); // 按名称查找
    }
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    // 创建编码器上下文
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }

    // 配置编码参数
    codec_ctx->codec_id = AV_CODEC_ID_AAC;
    codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
    codec_ctx->bit_rate = 128 * 1024;                // 128kbps
    codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO; // 立体声
    codec_ctx->sample_rate = 48000;                  // 48kHz采样率
    codec_ctx->channels = 2;                         // 双声道
    codec_ctx->profile = FF_PROFILE_AAC_LOW;         // AAC-LC配置文件

    // 根据编码器类型设置采样格式
    if (strcmp(codec->name, "aac") == 0) {
        codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; // FFmpeg原生AAC要求平面浮点
    } else if (strcmp(codec->name, "libfdk_aac") == 0) {
        codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16; // libfdk_aac要求16位整型
    } else {
        codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; // 默认平面浮点
    }

    // 检查编码器支持情况
    if (!check_sample_fmt(codec, codec_ctx->sample_fmt)) {
        fprintf(stderr, "Encoder does not support sample format %s",
                av_get_sample_fmt_name(codec_ctx->sample_fmt));
        exit(1);
    }
    if (!check_sample_rate(codec, codec_ctx->sample_rate)) {
        fprintf(stderr, "Encoder does not support sample rate %d", codec_ctx->sample_rate);
        exit(1);
    }
    if (!check_channel_layout(codec, codec_ctx->channel_layout)) {
        fprintf(stderr, "Encoder does not support channel layout %llu", codec_ctx->channel_layout);
        exit(1);
    }

    // 打印编码配置信息
    printf("\n\nAudio encode config\n");
    printf("bit_rate:%lldkbps\n", codec_ctx->bit_rate / 1024);
    printf("sample_rate:%d\n", codec_ctx->sample_rate);
    printf("sample_fmt:%s\n", av_get_sample_fmt_name(codec_ctx->sample_fmt));
    printf("channels:%d\n", codec_ctx->channels);
    printf("1 frame_size:%d\n", codec_ctx->frame_size); // 打开编码器前打印

    // 设置全局头标志（影响ADTS头的生成）
    codec_ctx->flags = AV_CODEC_FLAG_GLOBAL_HEADER;

    // 打开编码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
    printf("2 frame_size:%d\n\n", codec_ctx->frame_size); // 打开后打印实际帧大小

    // 打开输入输出文件
    FILE *infile = fopen(in_pcm_file, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s\n", in_pcm_file);
        exit(1);
    }
    FILE *outfile = fopen(out_aac_file, "wb");
    if (!outfile) {
        fprintf(stderr, "Could not open %s\n", out_aac_file);
        exit(1);
    }

    // 分配数据包和帧
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "could not allocate the packet\n");
        exit(1);
    }
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }

    // 配置音频帧参数
    frame->nb_samples = codec_ctx->frame_size;         // 每帧采样数
    frame->format = codec_ctx->sample_fmt;             // 采样格式
    frame->channel_layout = codec_ctx->channel_layout; // 声道布局
    frame->channels = codec_ctx->channels;             // 声道数

    // 打印帧配置
    printf("frame nb_samples:%d\n", frame->nb_samples);
    printf("frame sample_fmt:%d\n", frame->format);
    printf("frame channel_layout:%lu\n\n", frame->channel_layout);

    // 为帧分配缓冲区
    int ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate audio data buffers\n");
        exit(1);
    }

    // 计算每帧字节大小
    int frame_bytes = av_get_bytes_per_sample(frame->format) * frame->channels * frame->nb_samples;
    printf("frame_bytes %d\n", frame_bytes);

    // 分配PCM读取缓冲区
    uint8_t *pcm_buf = (uint8_t *)malloc(frame_bytes);
    if (!pcm_buf) {
        fprintf(stderr, "pcm_buf malloc failed\n");
        return 1;
    }
    uint8_t *pcm_temp_buf = (uint8_t *)malloc(frame_bytes);
    if (!pcm_temp_buf) {
        fprintf(stderr, "pcm_temp_buf malloc failed\n");
        return 1;
    }

    // 主编码循环
    int64_t pts = 0;
    printf("start encode\n");
    for (;;) {
        memset(pcm_buf, 0, frame_bytes);
        size_t read_bytes = fread(pcm_buf, 1, frame_bytes, infile);
        if (read_bytes <= 0) {
            printf("read file finish\n");
            break; // 文件读取结束
        }

        // 确保帧可写
        ret = av_frame_make_writable(frame);
        if (ret != 0) {
            fprintf(stderr, "av_frame_make_writable failed, ret = %d\n", ret);
        }

        // 根据采样格式处理数据
        if (AV_SAMPLE_FMT_S16 == frame->format) {
            // S16格式直接填充
            ret = av_samples_fill_arrays(frame->data, frame->linesize, pcm_buf, frame->channels,
                                         frame->nb_samples, frame->format, 0);
        } else {
            // FLTP格式需要转换，默认AAC编码为FLTP
            memset(pcm_temp_buf, 0, frame_bytes);
            //
            f32le_convert_to_fltp((float *)pcm_buf, (float *)pcm_temp_buf, frame->nb_samples);
            ret = av_samples_fill_arrays(frame->data, frame->linesize, pcm_temp_buf,
                                         frame->channels, frame->nb_samples, frame->format, 0);
        }

        // 设置时间戳并编码
        pts += frame->nb_samples;
        frame->pts = pts; // 以采样数为单位的时间戳
        ret = encode(codec_ctx, frame, pkt, outfile);
        if (ret < 0) {
            fprintf(stderr, "encode failed\n");
            break;
        }
    }

    // 刷新编码器（发送NULL帧）
    encode(codec_ctx, NULL, pkt, outfile);

    // 资源清理
    fclose(infile);
    fclose(outfile);
    free(pcm_buf);
    free(pcm_temp_buf);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);

    printf("main finish, please enter Enter and exit\n");
    getchar(); // 等待用户输入（便于调试）
    return 0;
}