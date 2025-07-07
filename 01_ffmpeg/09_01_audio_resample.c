/**
 * @example resampling_audio.c
 * libswresample API 使用示例：展示如何使用 libswresample 进行音频重采样
 */

#include <libavutil/opt.h>            // 包含 FFmpeg 常用选项相关函数
#include <libavutil/channel_layout.h> // 包含音频通道布局相关定义
#include <libavutil/samplefmt.h>      // 包含音频样本格式相关定义
#include <libswresample/swresample.h> // 包含音频重采样库的主要接口

/**
 * 根据样本格式获取对应的格式字符串
 * @param fmt 输出参数，返回格式字符串
 * @param sample_fmt 输入参数，样本格式枚举值
 * @return 0 表示成功，负数表示失败
 */
static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt)
{
    int i;
    // 样本格式映射表，包含大端和小端表示
    struct sample_fmt_entry
    {
        enum AVSampleFormat sample_fmt; // 样本格式枚举值
        const char *fmt_be, *fmt_le;    // 大端和小端格式字符串
    } sample_fmt_entries[] = {
        {AV_SAMPLE_FMT_U8, "u8", "u8"},        // 无符号 8 位整数
        {AV_SAMPLE_FMT_S16, "s16be", "s16le"}, // 有符号 16 位整数
        {AV_SAMPLE_FMT_S32, "s32be", "s32le"}, // 有符号 32 位整数
        {AV_SAMPLE_FMT_FLT, "f32be", "f32le"}, // 32 位浮点数
        {AV_SAMPLE_FMT_DBL, "f64be", "f64le"}, // 64 位浮点数
    };
    *fmt = NULL;

    // 遍历样本格式映射表，查找匹配的格式
    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++)
    {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt)
        {
            // 根据系统字节序选择大端或小端格式字符串
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    // 未找到匹配的格式时输出错误信息
    fprintf(stderr,
            "样本格式 %s 不支持作为输出格式\n",
            av_get_sample_fmt_name(sample_fmt));
    return AVERROR(EINVAL);
}

/**
 * 生成测试音频样本数据（正弦波）
 * @param dst 目标缓冲区指针
 * @param nb_samples 样本数量
 * @param nb_channels 通道数量
 * @param sample_rate 采样率
 * @param t 时间指针，用于生成连续的音频数据
 */
static void fill_samples(double *dst, int nb_samples, int nb_channels, int sample_rate, double *t)
{
    int i, j;
    // 计算时间增量（每个样本的时间间隔）
    double tincr = 1.0 / sample_rate, *dstp = dst;
    // 440Hz 正弦波的角频率（2πf）
    const double c = 2 * M_PI * 440.0;

    // 生成 440Hz 正弦波音频数据，并复制到所有通道
    for (i = 0; i < nb_samples; i++)
    {
        // 计算当前时间点的正弦值
        *dstp = sin(c * *t);
        // 将相同的样本值复制到所有通道（模拟立体声）
        for (j = 1; j < nb_channels; j++)
            dstp[j] = dstp[0];
        // 移动到下一组样本
        dstp += nb_channels;
        // 更新时间
        *t += tincr;
    }
}

int main(int argc, char **argv)
{
    // 输入音频参数
    int64_t src_ch_layout = AV_CH_LAYOUT_STEREO;            // 输入通道布局：立体声
    int src_rate = 48000;                                   // 输入采样率：48kHz
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_DBL; // 输入样本格式：64位浮点数
    int src_nb_channels = 0;                                // 输入通道数量（动态计算）
    uint8_t **src_data = NULL;                              // 输入样本数据缓冲区（二级指针）
    int src_linesize;                                       // 输入样本行大小
    int src_nb_samples = 1024;                              // 输入样本数量

    // 输出音频参数
    int64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;            // 输出通道布局：立体声
    int dst_rate = 44100;                                   // 输出采样率：44.1kHz
    enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16; // 输出样本格式：16位有符号整数
    int dst_nb_channels = 0;                                // 输出通道数量（动态计算）
    uint8_t **dst_data = NULL;                              // 输出样本数据缓冲区（二级指针）
    int dst_linesize;                                       // 输出样本行大小
    int dst_nb_samples;                                     // 输出样本数量
    int max_dst_nb_samples;                                 // 最大输出样本数量

    // 输出文件相关
    const char *dst_filename = NULL; // 输出文件名
    FILE *dst_file;                  // 输出文件句柄

    int dst_bufsize; // 输出缓冲区大小
    const char *fmt; // 输出格式字符串

    // 重采样上下文
    struct SwrContext *swr_ctx; // 重采样上下文指针

    double t; // 时间变量
    int ret;  // 函数返回值

    // 检查命令行参数
    if (argc != 2)
    {
        fprintf(stderr, "用法: %s 输出文件\n"
                        "API 示例程序，展示如何使用 libswresample 重采样音频流。\n"
                        "此程序生成一系列音频帧，将其重采样为指定的输出格式和速率，并保存到名为输出文件的文件中。\n",
                argv[0]);
        exit(1);
    }
    dst_filename = argv[1];

    // 打开输出文件
    dst_file = fopen(dst_filename, "wb");
    if (!dst_file)
    {
        fprintf(stderr, "无法打开目标文件 %s\n", dst_filename);
        exit(1);
    }

    // 创建重采样上下文
    swr_ctx = swr_alloc();
    if (!swr_ctx)
    {
        fprintf(stderr, "无法分配重采样上下文\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    // 设置重采样参数
    // 设置输入参数
    av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);     // 输入通道布局
    av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);             // 输入采样率
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0); // 输入样本格式
    // 设置输出参数
    av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);     // 输出通道布局
    av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);             // 输出采样率
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0); // 输出样本格式

    // 初始化重采样上下文
    if ((ret = swr_init(swr_ctx)) < 0)
    {
        fprintf(stderr, "重采样上下文初始化失败\n");
        goto end;
    }

    // 分配输入样本缓冲区
    src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout); // 获取输入通道数量
    // 分配数组和样本缓冲区
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels,
                                             src_nb_samples, src_sample_fmt, 0);
    if (ret < 0)
    {
        fprintf(stderr, "无法分配输入样本\n");
        goto end;
    }

    // 计算输出样本数量（考虑重采样延迟）
    max_dst_nb_samples = dst_nb_samples =
        av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);

    // 分配输出样本缓冲区
    dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout); // 获取输出通道数量
    // 分配数组和样本缓冲区
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dst_nb_samples, dst_sample_fmt, 0);
    if (ret < 0)
    {
        fprintf(stderr, "无法分配输出样本\n");
        goto end;
    }

    t = 0;
    // 生成并重采样音频数据，持续10秒
    do
    {
        // 生成合成音频数据（440Hz正弦波）
        fill_samples((double *)src_data[0], src_nb_samples, src_nb_channels, src_rate, &t);

        // 计算输出样本数量（考虑重采样延迟）
        int64_t delay = swr_get_delay(swr_ctx, src_rate);
        dst_nb_samples = av_rescale_rnd(delay + src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
        // 如果需要更大的输出缓冲区，则重新分配
        if (dst_nb_samples > max_dst_nb_samples)
        {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
                                   dst_nb_samples, dst_sample_fmt, 1);
            if (ret < 0)
                break;
            max_dst_nb_samples = dst_nb_samples;
        }

        // 执行音频重采样转换
        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
        if (ret < 0)
        {
            fprintf(stderr, "重采样转换错误\n");
            goto end;
        }
        // 计算输出缓冲区大小
        dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                                 ret, dst_sample_fmt, 1);
        if (dst_bufsize < 0)
        {
            fprintf(stderr, "无法获取样本缓冲区大小\n");
            goto end;
        }
        // 输出处理进度信息
        printf("时间:%f 输入样本数:%d 输出样本数:%d\n", t, src_nb_samples, ret);
        // 将重采样后的音频数据写入文件
        fwrite(dst_data[0], 1, dst_bufsize, dst_file);
    } while (t < 10); // 持续生成10秒的音频数据

    // 刷新重采样器，处理剩余样本
    ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, NULL, 0);
    if (ret < 0)
    {
        fprintf(stderr, "重采样转换错误\n");
        goto end;
    }
    // 计算输出缓冲区大小
    dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                             ret, dst_sample_fmt, 1);
    if (dst_bufsize < 0)
    {
        fprintf(stderr, "无法获取样本缓冲区大小\n");
        goto end;
    }
    // 输出刷新信息
    printf("刷新 输入样本数:%d 输出样本数:%d\n", 0, ret);
    // 将剩余音频数据写入文件
    fwrite(dst_data[0], 1, dst_bufsize, dst_file);

    // 获取输出格式字符串并提示播放命令
    if ((ret = get_format_from_sample_fmt(&fmt, dst_sample_fmt)) < 0)
        goto end;
    fprintf(stderr, "重采样成功。使用以下命令播放输出文件：\n"
                    "ffplay -f %s -channel_layout %" PRId64 " -channels %d -ar %d %s\n",
            fmt, dst_ch_layout, dst_nb_channels, dst_rate, dst_filename);

end:
    // 关闭输出文件
    fclose(dst_file);

    // 释放输入样本缓冲区
    if (src_data)
    {
        av_freep(&src_data[0]);
        av_freep(&src_data);
    }

    // 释放输出样本缓冲区
    if (dst_data)
    {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }

    // 释放重采样上下文
    swr_free(&swr_ctx);
    return ret < 0; // 返回错误码
}