#include <stdio.h>
#include <libavutil/log.h>      // FFmpeg日志模块
#include <libavformat/avio.h>   // FFmpeg I/O操作
#include <libavformat/avformat.h> // FFmpeg格式处理

#define ADTS_HEADER_LEN  7;     // ADTS头固定长度（7字节）

// AAC标准采样频率映射表（对应ISO/IEC 14496-3标准）
const int sampling_frequencies[] = {
    96000,  // 0x0
    88200,  // 0x1
    64000,  // 0x2
    48000,  // 0x3 (默认值)
    44100,  // 0x4
    32000,  // 0x5
    24000,  // 0x6
    22050,  // 0x7
    16000,  // 0x8
    12000,  // 0x9
    11025,  // 0xa
    8000    // 0xb
    // 注意：0xc, 0xd, 0xe, 0xf 保留
};

/**
 * 生成ADTS音频头
 * @param p_adts_header  存放生成的7字节ADTS头
 * @param data_length    AAC原始数据包长度（不含ADTS头）
 * @param profile        AAC编码规格（如FF_PROFILE_AAC_LOW）
 * @param samplerate     音频采样率（必须为标准值）
 * @param channels       声道数
 * @return 成功返回0，失败返回-1
 */
int adts_header(char * const p_adts_header, const int data_length,
                const int profile, const int samplerate,
                const int channels)
{
    int sampling_frequency_index = 3; // 默认48000Hz
    int adtsLen = data_length + 7;    // 完整帧长度（含ADTS头）

    // 查找匹配的采样率索引
    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for(i = 0; i < frequencies_size; i++) {
        if(sampling_frequencies[i] == samplerate) {
            sampling_frequency_index = i;
            break;
        }
    }
    if(i >= frequencies_size) {
        printf("不支持的采样率:%d\n", samplerate);
        return -1;
    }

    // ===== 按位构建ADTS头 =====
    // 第1字节：同步字高位 (0xFF)
    p_adts_header[0] = 0xff;
    
    // 第2字节：同步字低位 (0xF0) + 版本/层/保护标识
    p_adts_header[1] = 0xf0;          // 0b11110000
    p_adts_header[1] |= (0 << 3);      // MPEG版本: 0=MPEG-4, 1=MPEG-2 (位3)
    p_adts_header[1] |= (0 << 1);      // 层: 00 (位1-2)
    p_adts_header[1] |= 1;             // 保护缺失标志: 1=无CRC (位0)

    // 第3字节：配置信息
    p_adts_header[2] = (profile)<<6;  // AAC规格 (位6-7)
    p_adts_header[2] |= (sampling_frequency_index & 0x0f)<<2; // 采样率索引 (位2-5)
    p_adts_header[2] |= (0 << 1);     // 私有位: 0 (位1)
    p_adts_header[2] |= (channels & 0x04)>>2; // 声道数高位 (位0)

    // 第4字节：声道/原版/长度高位
    p_adts_header[3] = (channels & 0x03)<<6; // 声道数低位 (位6-7)
    p_adts_header[3] |= (0 << 5);     // 原版标志: 0 (位5)
    p_adts_header[3] |= (0 << 4);     // 主页标志: 0 (位4)
    p_adts_header[3] |= (0 << 3);     // 版权ID: 0 (位3)
    p_adts_header[3] |= (0 << 2);     // 版权起始: 0 (位2)
    p_adts_header[3] |= ((adtsLen & 0x1800) >> 11); // 帧长度高位 (位0-1)

    // 第5字节：帧长度中位
    p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3); // 帧长度中间8位 (位0-7)
    
    // 第6字节：帧长度低位 + 缓冲区填充
    p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);   // 帧长度低位 (位5-7)
    p_adts_header[5] |= 0x1f;         // 缓冲区填充度0x7FF高位 (位0-4)

    // 第7字节：缓冲区填充低位
    p_adts_header[6] = 0xfc;           // 缓冲区填充度0x7FF低位 (0b11111100)

    return 0;
}

int main(int argc, char *argv[])
{
    // 初始化和错误处理
    int ret = -1;
    char errors[1024];  // 存放FFmpeg错误信息

    // 文件路径
    char *in_filename = NULL;    // 输入文件（视频/音频）
    char *aac_filename = NULL;    // 输出AAC文件

    FILE *aac_fd = NULL;         // 输出文件句柄

    // 音频流索引和临时变量
    int audio_index = -1;
    int len = 0;

    // FFmpeg结构体
    AVFormatContext *ifmt_ctx = NULL;  // 输入格式上下文
    AVPacket pkt;                      // 数据包容器

    // 设置FFmpeg日志级别（调试模式）
    av_log_set_level(AV_LOG_DEBUG);

    // ===== 1. 参数校验 =====
    if(argc < 3) {
        av_log(NULL, AV_LOG_ERROR, "参数不足！用法: %s <输入文件> <输出AAC文件>\n", argv[0]);
        return -1;
    }
    in_filename = argv[1];   // 第一个参数：输入文件
    aac_filename = argv[2];  // 第二个参数：输出文件

    // ===== 2. 打开输出文件 =====
    aac_fd = fopen(aac_filename, "wb");
    if (!aac_fd) {
        av_log(NULL, AV_LOG_ERROR, "无法打开输出文件: %s\n", aac_filename);
        return -1;
    }

    // ===== 3. 打开输入文件 =====
    if((ret = avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL)) < 0) {
        av_strerror(ret, errors, sizeof(errors));
        av_log(NULL, AV_LOG_ERROR, "文件打开失败: %s, 错误: %s\n", in_filename, errors);
        goto cleanup;
    }

    // ===== 4. 获取流信息 =====
    if((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_strerror(ret, errors, sizeof(errors));
        av_log(NULL, AV_LOG_ERROR, "流信息获取失败: %s\n", errors);
        goto cleanup;
    }

    // 打印媒体信息（调试用）
    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    // ===== 5. 定位音频流 =====
    audio_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if(audio_index < 0) {
        av_log(NULL, AV_LOG_ERROR, "找不到音频流\n");
        ret = AVERROR(EINVAL);
        goto cleanup;
    }

    // 检查是否AAC流
    AVCodecParameters *codecpar = ifmt_ctx->streams[audio_index]->codecpar;
    if(codecpar->codec_id != AV_CODEC_ID_AAC) {
        av_log(NULL, AV_LOG_ERROR, "非AAC音频流（编码ID：%d）\n", codecpar->codec_id);
        ret = -1;
        goto cleanup;
    }

    // 打印音频规格信息
    av_log(NULL, AV_LOG_INFO, "音频规格: %d (0=Main,1=LC,2=SSR), 采样率: %d, 声道: %d\n",
           codecpar->profile, codecpar->sample_rate, codecpar->channels);

    // ===== 6. 主处理循环 =====
    av_init_packet(&pkt);  // 初始化数据包
    while(av_read_frame(ifmt_ctx, &pkt) >=0 ) {
        if(pkt.stream_index == audio_index) {
            char adts_header_buf[7];  // ADTS头缓存

            // 生成ADTS头
            if(adts_header(adts_header_buf, pkt.size,
                           codecpar->profile,
                           codecpar->sample_rate,
                           codecpar->channels) != 0) {
                av_log(NULL, AV_LOG_WARNING, "ADTS头生成失败，跳过帧\n");
            } else {
                // 写入ADTS头
                fwrite(adts_header_buf, 1, sizeof(adts_header_buf), aac_fd);
                // 写入原始AAC数据
                len = fwrite(pkt.data, 1, pkt.size, aac_fd);
                if(len != pkt.size) {
                    av_log(NULL, AV_LOG_WARNING, 
                           "写入不完整: %d/%d 字节\n", len, pkt.size);
                }
            }
        }
        av_packet_unref(&pkt);  // 释放数据包资源
    }

    av_log(NULL, AV_LOG_INFO, "AAC提取完成\n");

// ===== 7. 资源清理 =====
cleanup:
    if(ifmt_ctx) avformat_close_input(&ifmt_ctx);  // 关闭输入文件
    if(aac_fd) fclose(aac_fd);                     // 关闭输出文件
    return ret < 0 ? -1 : 0;
}