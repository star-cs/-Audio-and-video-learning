/**
 * @file
 * libavformat API example.
 *
 * 输出支持任意libavformat格式的媒体文件，使用默认编解码器
 * @example muxing.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

// 定义流参数
#define STREAM_DURATION 5.0               // 流持续时间（秒）
#define STREAM_FRAME_RATE 25              // 帧率（25帧/秒）
#define STREAM_PIX_FMT AV_PIX_FMT_YUV420P // 默认像素格式
#define SCALE_FLAGS SWS_BICUBIC           // 图像缩放算法标志

// 封装单个输出流的结构体
typedef struct OutputStream {
    AVStream *st;        // 代表一个流（音频/视频）
    AVCodecContext *enc; // 编码器上下文

    /* 下一帧的显示时间戳 */
    int64_t next_pts;  // 决定着下一帧写音频还是视频

    int samples_count; // 音频采样计数

    AVFrame *frame;     // 处理后的帧（重采样/缩放后）
    AVFrame *tmp_frame; // 原始帧（处理前）

    // 音频生成参数：时间、时间增量
    float t, tincr, tincr2;

    // 处理上下文
    struct SwsContext *sws_ctx; // 图像缩放上下文
    struct SwrContext *swr_ctx; // 音频重采样上下文
} OutputStream;

// 打印数据包信息
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base), av_ts2str(pkt->dts),
           av_ts2timestr(pkt->dts, time_base), av_ts2str(pkt->duration),
           av_ts2timestr(pkt->duration, time_base), pkt->stream_index);
}

// 写入帧到媒体文件
static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st,
                       AVPacket *pkt)
{
    /* 将数据包时间戳从编解码器时间基转换为流时间基 */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* 将压缩帧写入媒体文件 */
    log_packet(fmt_ctx, pkt); // 保留日志输出
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

// 添加输出流
static void add_stream(OutputStream *ost, AVFormatContext *oc, AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *codec_ctx;
    int i;

    /* 查找编码器 */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "找不到编码器: '%s'\n", avcodec_get_name(codec_id));
        exit(1);
    }

    /* 创建新流 */
    // 第二个参数一般为NULL
    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "无法分配流\n");
        exit(1);
    }
    // 创建了流，oc->nb_streams 就+1了。但是id从0开始
    ost->st->id = oc->nb_streams - 1; // 流ID分配

    /* 创建编码器上下文 */
    codec_ctx = avcodec_alloc_context3(*codec);
    if (!codec_ctx) {
        fprintf(stderr, "无法分配编码上下文\n");
        exit(1);
    }
    ost->enc = codec_ctx;

    /* 根据流类型初始化参数 */
    switch ((*codec)->type) {
        case AVMEDIA_TYPE_AUDIO: // 音频流设置
            codec_ctx->codec_id = codec_id;
            // 设置采样格式（优先使用编码器支持的第一种格式）
            codec_ctx->sample_fmt =
                (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            codec_ctx->bit_rate = 64000;    // 比特率
            codec_ctx->sample_rate = 44100; // 采样率

            // 选择支持的采样率
            if ((*codec)->supported_samplerates) {
                codec_ctx->sample_rate = (*codec)->supported_samplerates[0];
                for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                    if ((*codec)->supported_samplerates[i] == 44100)
                        codec_ctx->sample_rate = 44100;
                }
            }

            // 设置声道布局
            codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
            codec_ctx->channels = av_get_channel_layout_nb_channels(codec_ctx->channel_layout);

            // 选择支持的声道布局
            if ((*codec)->channel_layouts) {
                codec_ctx->channel_layout = (*codec)->channel_layouts[0];
                for (i = 0; (*codec)->channel_layouts[i]; i++) {
                    if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                        codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
                }
            }
            codec_ctx->channels = av_get_channel_layout_nb_channels(codec_ctx->channel_layout);

            // 设置时间基（1/采样率）
            ost->st->time_base = (AVRational){1, codec_ctx->sample_rate};
            break;

        case AVMEDIA_TYPE_VIDEO: // 视频流设置
            codec_ctx->codec_id = codec_id;
            codec_ctx->bit_rate = 400000; // 比特率
            codec_ctx->width = 352;       // 分辨率（必须是偶数）
            codec_ctx->height = 288;
            codec_ctx->max_b_frames = 1; // B帧数量

            /* 时间基 = 1/帧率 */
            ost->st->time_base = (AVRational){1, STREAM_FRAME_RATE};
            codec_ctx->time_base = ost->st->time_base; // 使用相同时间基

            codec_ctx->gop_size = STREAM_FRAME_RATE; // 关键帧间隔
            codec_ctx->pix_fmt = STREAM_PIX_FMT;     // 像素格式，就是 yuv420p
            break;

        default:
            break;
    }

    /* 某些格式需要单独的流头 */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* 音频输出处理 */

// 分配音频帧
static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt, uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        fprintf(stderr, "无法分配音频帧\n");
        exit(1);
    }

    // 设置帧参数
    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    // 分配缓冲区
    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "无法分配音频缓冲区\n");
            exit(1);
        }
    }

    return frame;
}

// 打开音频编码器并初始化资源
static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost,
                       AVDictionary *opt_arg)
{
    AVCodecContext *codec_ctx;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;

    codec_ctx = ost->enc;

    /* 打开编码器 */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(codec_ctx, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "无法打开音频编码器: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 初始化信号生成器参数 */
    ost->t = 0;
    ost->tincr = 2 * M_PI * 110.0 / codec_ctx->sample_rate; // 基础频率
    ost->tincr2 = 2 * M_PI * 110.0 / codec_ctx->sample_rate / codec_ctx->sample_rate; // 频率增量

    /* 确定帧大小 */
    nb_samples = codec_ctx->frame_size; // 使用编码器默认帧大小

    /* 分配帧 */
    // 编码器输入帧（处理后的格式）
    ost->frame = alloc_audio_frame(codec_ctx->sample_fmt, codec_ctx->channel_layout,
                                   codec_ctx->sample_rate, nb_samples);
    // 原始PCM帧（S16格式）
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, codec_ctx->channel_layout,
                                       codec_ctx->sample_rate, nb_samples);

    /* 将流参数复制到复用器 */
    ret = avcodec_parameters_from_context(ost->st->codecpar, codec_ctx);
    if (ret < 0) {
        fprintf(stderr, "无法复制流参数\n");
        exit(1);
    }

    /* 创建重采样上下文 */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        fprintf(stderr, "无法分配重采样上下文\n");
        exit(1);
    }

    /* 配置重采样参数 */
    av_opt_set_int(ost->swr_ctx, "in_channel_count", codec_ctx->channels, 0);
    av_opt_set_int(ost->swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int(ost->swr_ctx, "out_channel_count", codec_ctx->channels, 0);
    av_opt_set_int(ost->swr_ctx, "out_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", codec_ctx->sample_fmt, 0);

    /* 初始化重采样器 */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        fprintf(stderr, "无法初始化重采样上下文: %s\n", av_err2str(ret));
        exit(1);
    }
}

// 生成音频帧（正弦波）
static AVFrame *get_audio_frame(OutputStream *ost)
{
    AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    int16_t *q = (int16_t *)frame->data[0];

    /* 检查是否需要生成更多帧 */
    if (av_compare_ts(ost->next_pts, ost->enc->time_base, STREAM_DURATION, (AVRational){1, 1}) >= 0)
        return NULL;

    /* 生成正弦波PCM数据 */
    for (j = 0; j < frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000); // 正弦波值
        for (i = 0; i < ost->enc->channels; i++)
            *q++ = v;              // 填充到所有声道
        ost->t += ost->tincr;      // 更新相位
        ost->tincr += ost->tincr2; // 更新频率
    }

    /* 设置时间戳 */
    frame->pts = ost->next_pts;
    ost->next_pts += frame->nb_samples; // 按采样数递增

    return frame;
}

// 编码并写入音频帧
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *codec_ctx;
    AVPacket pkt = {0};
    AVFrame *frame;
    int ret;
    int got_packet;
    int dst_nb_samples;

    av_init_packet(&pkt);
    codec_ctx = ost->enc;

    /* 获取原始音频帧 */
    frame = get_audio_frame(ost);

    if (frame) {
        /* 计算重采样后的样本数 */
        dst_nb_samples =
            av_rescale_rnd(swr_get_delay(ost->swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                           codec_ctx->sample_rate, codec_ctx->sample_rate, AV_ROUND_UP);
        av_assert0(dst_nb_samples == frame->nb_samples);

        /* 确保目标帧可写 */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            exit(1);

        /* 执行重采样 */
        ret = swr_convert(ost->swr_ctx, ost->frame->data, dst_nb_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "重采样错误\n");
            exit(1);
        }
        frame = ost->frame; // 使用重采样后的帧

        /* 重计算时间戳 */
        frame->pts = av_rescale_q(ost->samples_count, (AVRational){1, codec_ctx->sample_rate},
                                  codec_ctx->time_base);
        ost->samples_count += dst_nb_samples; // 更新总采样数
    }

    /* 编码音频帧 */
    ret = avcodec_encode_audio2(codec_ctx, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "音频编码错误: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 写入编码后的数据包 */
    if (got_packet) {
        ret = write_frame(oc, &codec_ctx->time_base, ost->st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "写入音频帧错误: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    /* 返回状态：0=继续，1=结束 */
    return (frame || got_packet) ? 0 : 1;
}

/**************************************************************/
/* 视频输出处理 */

// 分配视频帧
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    // 设置帧参数
    picture->format = pix_fmt;
    picture->width = width;
    picture->height = height;

    /* 分配帧缓冲区 */
    ret = av_frame_get_buffer(picture, 32); // 32字节对齐
    if (ret < 0) {
        fprintf(stderr, "无法分配视频帧数据\n");
        exit(1);
    }

    return picture;
}

// 打开视频编码器并初始化资源
static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost,
                       AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *codec_ctx = ost->enc;
    AVDictionary *opt = NULL;

    /* 打开编码器 */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(codec_ctx, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "无法打开视频编码器: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 分配编码器输入帧 */
    ost->frame = alloc_picture(codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height);
    if (!ost->frame) {
        fprintf(stderr, "无法分配视频帧\n");
        exit(1);
    }

    /* 如果编码器格式不是YUV420P，需要额外分配临时帧 */
    /**
    生成阶段：用 fill_yuv_image() 生成数据到 tmp_frame（YUV420P）
    转换阶段：使用 sws_scale() 将 tmp_frame 转换为 frame（目标格式）    
    编码阶段：将转换后的 frame 送给编码器
    */
    ost->tmp_frame = NULL;
    if (codec_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        // scale 操作
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "无法分配临时图像\n");
            exit(1);
        }
    }

    /* 将流参数复制到复用器 */
    ret = avcodec_parameters_from_context(ost->st->codecpar, codec_ctx);
    if (ret < 0) {
        fprintf(stderr, "无法复制流参数\n");
        exit(1);
    }
}

// 填充YUV图像数据
static void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
    int x, y, i = frame_index;

    /* 填充Y分量 */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* 填充Cb和Cr分量 */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

// 获取视频帧
static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext *codec_ctx = ost->enc;

    /* 检查是否需要生成更多帧 */
    if (av_compare_ts(ost->next_pts, codec_ctx->time_base, STREAM_DURATION, (AVRational){1, 1})
        >= 0)
        return NULL;

    /* 确保帧可写 */
    if (av_frame_make_writable(ost->frame) < 0)
        exit(1);

    /* 处理非YUV420P格式的转换 */
    if (codec_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* 初始化缩放上下文（如果需要） */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,
                                          codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr, "无法初始化转换上下文\n");
                exit(1);
            }
        }

        /* 填充临时帧（YUV420P格式） */
        fill_yuv_image(ost->tmp_frame, ost->next_pts, codec_ctx->width, codec_ctx->height);

        /* 转换到目标格式 */
        sws_scale(ost->sws_ctx, (const uint8_t *const *)ost->tmp_frame->data,
                  ost->tmp_frame->linesize, 0, codec_ctx->height, ost->frame->data,
                  ost->frame->linesize);
    } else {
        /* 直接填充目标帧 */
        fill_yuv_image(ost->frame, ost->next_pts, codec_ctx->width, codec_ctx->height);
    }

    /* 设置时间戳并递增 */
    ost->frame->pts = ost->next_pts++;
    return ost->frame;
}

// 编码并写入视频帧
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    int ret;
    AVCodecContext *codec_ctx;
    AVFrame *frame;
    int got_packet = 0;
    AVPacket pkt = {0};

    codec_ctx = ost->enc;
    av_init_packet(&pkt);

    /* 获取视频帧 */
    frame = get_video_frame(ost);

    /* 编码视频帧 */
    ret = avcodec_encode_video2(codec_ctx, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "视频编码错误: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 写入编码后的数据包 */
    if (got_packet) {
        ret = write_frame(oc, &codec_ctx->time_base, ost->st, &pkt);
    } else {
        ret = 0;
    }

    if (ret < 0) {
        fprintf(stderr, "写入视频帧错误: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 返回状态：0=继续，1=结束 */
    return (frame || got_packet) ? 0 : 1;
}

// 关闭流并释放资源
static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* 主函数 */

int main(int argc, char **argv)
{
    OutputStream video_st = {0};        // 视频流
    OutputStream audio_st = {0};        // 音频流
    const char *filename;               // 输出文件名
    AVOutputFormat *fmt;                // 输出格式
    AVFormatContext *oc;                // 格式上下文
    AVCodec *audio_codec, *video_codec; // 编解码器
    int ret;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;
    AVDictionary *opt = NULL; // 选项字典
    int i;

    /* 参数检查 */
    if (argc < 2) {
        printf("用法: %s 输出文件\n"
               "使用libavformat输出媒体文件的API示例程序\n"
               "该程序生成合成的音频和视频流，编码后混合到指定文件\n"
               "输出格式根据文件扩展名自动猜测\n"
               "原始图像可通过文件名中使用'%%d'输出\n\n",
               argv[0]);
        return 1;
    }

    filename = argv[1];

    /* 解析额外选项 */
    for (i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
            av_dict_set(&opt, argv[i] + 1, argv[i + 1], 0);
    }

    /* 分配输出上下文 */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        /* 回退到FLV格式 */
        printf("无法从扩展名推断输出格式，使用flv\n");
        avformat_alloc_output_context2(&oc, NULL, "flv", filename);
    }
    if (!oc)
        return 1;

    fmt = oc->oformat;

    /* 强制使用H.264视频和AAC音频 */
    fmt->video_codec = AV_CODEC_ID_H264;
    fmt->audio_codec = AV_CODEC_ID_AAC;

    /* 添加音视频流 */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }
 
    /* 打开编解码器并分配资源 */
    if (have_video)
        open_video(oc, video_codec, &video_st, opt);
    if (have_audio)
        open_audio(oc, audio_codec, &audio_st, opt);

    /* 打印格式信息 */
    av_dump_format(oc, 0, filename, 1);

    /* 打开输出文件 */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "无法打开'%s': %s\n", filename, av_err2str(ret));
            return 1;
        }
    }

    // audio AVstream->base_time = 1/44100, video AVstream->base_time = 1/25
    /* 写入文件头 */
    ret = avformat_write_header(oc, &opt);
    // base_time audio = 1/1000 video = 1/1000      1毫秒

    if (ret < 0) {
        fprintf(stderr, "打开输出文件时出错: %s\n", av_err2str(ret));
        return 1;
    }

    /* 主编码循环 */
    while (encode_video || encode_audio) {
        /* 基于时间戳选择要编码的流 */
        if (encode_video
            && (!encode_audio
                || av_compare_ts(video_st.next_pts, video_st.enc->time_base, audio_st.next_pts,
                                 audio_st.enc->time_base)
                       <= 0)) {
            printf("\n写入视频帧\n"); // 保留日志
            encode_video = !write_video_frame(oc, &video_st);
        } else {
            printf("\n写入音频帧\n"); // 保留日志
            encode_audio = !write_audio_frame(oc, &audio_st);
        }
    }

    /* 写入文件尾 */
    av_write_trailer(oc);

    /* 清理资源 */
    if (have_video)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        avio_closep(&oc->pb); // 关闭输出文件

    avformat_free_context(oc); // 释放格式上下文

    return 0;
}