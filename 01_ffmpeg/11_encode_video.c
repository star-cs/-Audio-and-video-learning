/**
* @brief         视频编码，从本地读取YUV数据进行H264编码
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

// 获取当前时间（毫秒）
int64_t get_time()
{
    return av_gettime_relative() / 1000; // 换算成毫秒
}

/**
 * @brief 编码帧并写入输出文件
 * @param enc_ctx 编码器上下文
 * @param frame 待编码的AVFrame（NULL表示冲刷编码器）
 * @param pkt 用于存储编码后数据的AVPacket
 * @param outfile 输出文件指针
 * @return 成功返回0，失败返回负数
 */
static int encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3" PRId64 "\n", frame->pts);
    /* 通过查阅代码，使用x264进行编码时，具体缓存帧是在x264源码进行，
     * 不会增加avframe对应buffer的reference*/
    // 将帧提交给编码器
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        return -1;
    }

    // 循环接收所有可用的编码数据包
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            return -1;
        }

        // 打印关键帧和普通帧信息
        if (pkt->flags & AV_PKT_FLAG_KEY)
            printf("Write packet flags:%d pts:%3" PRId64 " dts:%3" PRId64 " (size:%5d)\n",
                   pkt->flags, pkt->pts, pkt->dts, pkt->size);
        if (!pkt->flags)
            printf("Write packet flags:%d pts:%3" PRId64 " dts:%3" PRId64 " (size:%5d)\n",
                   pkt->flags, pkt->pts, pkt->dts, pkt->size);

        // 将编码后的数据写入文件
        fwrite(pkt->data, 1, pkt->size, outfile);

        // 释放数据包引用计数
        av_packet_unref(pkt);
    }
    return 0;
}

/**
 * @brief 主函数：YUV转H264编码器
 * @note 提取测试文件命令：
 * ffmpeg -i test_1280x720.flv -t 5 -r 25 -pix_fmt yuv420p yuv420p_1280x720.yuv
 * 运行参数: <输入YUV> <输出H264> <编码器名称>
 * 示例: yuv420p_1280x720.yuv yuv420p_1280x720.h264 libx264
 */
int main(int argc, char **argv)
{
    char *in_yuv_file = NULL;   // 输入YUV文件路径
    char *out_h264_file = NULL; // 输出H264文件路径
    FILE *infile = NULL;        // 输入文件指针
    FILE *outfile = NULL;       // 输出文件指针

    const char *codec_name = NULL;    // 编码器名称
    const AVCodec *codec = NULL;      // FFmpeg编码器对象
    AVCodecContext *codec_ctx = NULL; // 编码器上下文
    AVFrame *frame = NULL;            // 帧容器
    AVPacket *pkt = NULL;             // 数据包容器
    int ret = 0;                      // 函数返回值检查

    // 参数校验
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input_file out_file codec_name >, argc:%d\n", argv[0], argc);
        return 0;
    }
    in_yuv_file = argv[1];   // 输入YUV文件
    out_h264_file = argv[2]; // 输出H264文件
    codec_name = argv[3];    // 编码器名称

    /* 查找指定的编码器 */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    // 创建编码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /********** 配置编码器参数 **********/
    codec_ctx->width = 1280;                    // 视频宽度
    codec_ctx->height = 720;                    // 视频高度
    codec_ctx->time_base = (AVRational){1, 25}; // 时间基 (1/25秒)
    codec_ctx->framerate = (AVRational){25, 1}; // 帧率 (25fps)

    /* 设置I帧间隔
     * 如果frame->pict_type设置为AV_PICTURE_TYPE_I, 则忽略gop_size的设置，一直当做I帧进行编码
     */
    codec_ctx->gop_size = 25;                // I帧间隔（每25帧一个关键帧）
    codec_ctx->max_b_frames = 2;             // B帧最大数量（0表示不使用B帧）
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // 像素格式

    // H264特有参数设置
    if (codec->id == AV_CODEC_ID_H264) {
        // 相关的参数可以参考libx264.c的 AVOption options
        // ultrafast all encode time:2270ms
        // medium all encode time:5815ms
        // veryslow all encode time:19836ms
        ret = av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);
        if (ret != 0) {
            printf("av_opt_set preset failed\n");
        }
        ret = av_opt_set(codec_ctx->priv_data, "profile", "main", 0); // 默认是high
        if (ret != 0) {
            printf("av_opt_set profile failed\n");
        }
        ret = av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0); // 直播时使用
        if (ret != 0) {
            printf("av_opt_set tune failed\n");
        }
    }

    /*
     * 设置编码器参数
    */
    codec_ctx->bit_rate = 3000000; // 目标码率 (3Mbps)
    // 多线程设置（注释状态，需要时可启用）
    // codec_ctx->thread_count = 4;
    // codec_ctx->thread_type = FF_THREAD_FRAME;

    /* 对于H264 AV_CODEC_FLAG_GLOBAL_HEADER  设置则只包含I帧，此时sps pps需要从codec_ctx->extradata读取
     *  不设置则每个I帧都带 sps pps sei
     */
    // 本地文件存储时不建议设置全局头标志
    // codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 打开编码器
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }
    // 打印线程配置信息
    printf("thread_count: %d, thread_type:%d\n", codec_ctx->thread_count, codec_ctx->thread_type);

    // 打开输入输出文件
    infile = fopen(in_yuv_file, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s\n", in_yuv_file);
        exit(1);
    }
    outfile = fopen(out_h264_file, "wb");
    if (!outfile) {
        fprintf(stderr, "Could not open %s\n", out_h264_file);
        exit(1);
    }

    // 分配数据包和帧内存
    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    // 配置帧参数
    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;

    // 为帧分配缓冲区
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    // 计算单帧YUV数据大小
    int frame_bytes = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);
    printf("frame_bytes %d\n", frame_bytes);

    // 分配YUV数据缓冲区
    uint8_t *yuv_buf = (uint8_t *)malloc(frame_bytes);
    if (!yuv_buf) {
        printf("yuv_buf malloc failed\n");
        return 1;
    }

    // 初始化时间统计变量
    int64_t begin_time = get_time();
    int64_t end_time = begin_time;
    int64_t all_begin_time = get_time();
    int64_t all_end_time = all_begin_time;
    int64_t pts = 0; // 显示时间戳

    printf("start encode\n");
    // 主编码循环
    for (;;) {
        memset(yuv_buf, 0, frame_bytes);
        // 从文件读取YUV数据
        size_t read_bytes = fread(yuv_buf, 1, frame_bytes, infile);
        if (read_bytes <= 0) {
            printf("read file finish\n");
            break;
        }

        /* 确保该frame可写, 如果编码器内部保持了内存参考计数，则需要重新拷贝一个备份
            目的是新写入的数据和编码器保存的数据不能产生冲突
        */
        int frame_is_writable = 1;
        if (av_frame_is_writable(frame) == 0) { // 这里只是用来测试
            printf("the frame can't write, buf:%p\n", frame->buf[0]);
            if (frame->buf && frame->buf[0]) // 打印referenc-counted，必须保证传入的是有效指针
                printf("ref_count1(frame) = %d\n", av_buffer_get_ref_count(frame->buf[0]));
            frame_is_writable = 0;
        }

        // 确保帧可写（必要时创建新缓冲区）
        ret = av_frame_make_writable(frame);
        if (frame_is_writable == 0) { // 这里只是用来测试
            printf("av_frame_make_writable, buf:%p\n", frame->buf[0]);
            if (frame->buf && frame->buf[0]) // 打印referenc-counted，必须保证传入的是有效指针
                printf("ref_count2(frame) = %d\n", av_buffer_get_ref_count(frame->buf[0]));
        }
        if (ret != 0) {
            printf("av_frame_make_writable failed, ret = %d\n", ret);
            break;
        }

        // 将YUV数据填充到帧的data缓冲区
        int need_size = av_image_fill_arrays(frame->data, frame->linesize, yuv_buf, frame->format,
                                             frame->width, frame->height, 1);
        if (need_size != frame_bytes) {
            printf("av_image_fill_arrays failed, need_size:%d, frame_bytes:%d\n", need_size,
                   frame_bytes);
            break;
        }

        // 更新显示时间戳（40ms = 1/25秒）
        pts += 40;
        frame->pts = pts; // 设置当前帧的显示时间戳

        // 执行编码
        begin_time = get_time();
        ret = encode(codec_ctx, frame, pkt, outfile);
        end_time = get_time();
        printf("encode time:%lldms\n", end_time - begin_time);
        if (ret < 0) {
            printf("encode failed\n");
            break;
        }
    }

    /* 冲刷编码器（处理缓冲区中剩余的帧） */
    encode(codec_ctx, NULL, pkt, outfile);

    // 计算总编码时间
    all_end_time = get_time();
    printf("all encode time:%lldms\n", all_end_time - all_begin_time);

    // 清理资源
    fclose(infile);
    fclose(outfile);

    if (yuv_buf) {
        free(yuv_buf);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);

    printf("main finish, please enter Enter and exit\n");
    getchar();
    return 0;
}