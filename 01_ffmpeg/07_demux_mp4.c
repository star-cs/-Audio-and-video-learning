#include <stdio.h>
#include <stdint.h> // 添加uint8_t类型支持

#include "libavutil/log.h"   // FFmpeg日志模块
#include "libavformat/avformat.h" // FFmpeg格式处理模块
#include "libavcodec/avcodec.h"   // FFmpeg编解码模块(bsf需要)

#define ERROR_STRING_SIZE 1024 // 错误信息缓冲区大小
#define ADTS_HEADER_LEN 7      // ADTS头部固定长度

// AAC采样率索引表 (参考ISO/IEC 14496-3标准)
const int sampling_frequencies[] = {
    96000, 88200, 64000, 48000, 44100, 
    32000, 24000, 22050, 16000, 12000, 
    11025, 8000   // 0x0-0xb对应标准采样率
    // 0xc-f为保留值
};

/**
 * 生成ADTS头部
 * @param p_adts_header 输出参数，7字节ADTS头部缓冲区
 * @param data_length AAC原始数据长度(不含ADTS头)
 * @param profile AAC配置信息 (0=MAIN, 1=LC, 2=SSR)
 * @param samplerate 实际采样率(Hz)
 * @param channels 声道数
 * @return 成功返回0，失败返回-1
 */
int adts_header(char * const p_adts_header, const int data_length,
                const int profile, const int samplerate,
                const int channels)
{
    int sampling_frequency_index = 3; // 默认48000Hz索引
    int adtsLen = data_length + ADTS_HEADER_LEN; // 含ADTS头的总长度

    // 查找匹配采样率
    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i;
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

    // 构建ADTS头部 (参考ISO/IEC 13818-7标准)
    // 字节1-2: 同步字和基础配置
    p_adts_header[0] = 0xff;        // 同步字高8位: 0xFF
    p_adts_header[1] = 0xf0;        // 同步字低4位: 0xF (11110000)
    p_adts_header[1] |= (0 << 3);   // MPEG版本: 0=MPEG-4
    p_adts_header[1] |= (0 << 1);   // 层: 0
    p_adts_header[1] |= 1;          // 保护缺失标志: 1=无CRC

    // 字节3: 配置信息
    p_adts_header[2] = (profile)<<6; // 配置描述(2位)
    p_adts_header[2] |= (sampling_frequency_index & 0x0f)<<2; // 采样率索引(4位)
    p_adts_header[2] |= (0 << 1);    // 私有位: 0
    p_adts_header[2] |= (channels & 0x04)>>2; // 声道配置高1位

    // 字节4: 声道和帧长度
    p_adts_header[3] = (channels & 0x03)<<6; // 声道配置低2位
    p_adts_header[3] |= (0 << 5);   // 原始标志: 0
    p_adts_header[3] |= (0 << 4);   // 家庭标志: 0
    p_adts_header[3] |= (0 << 3);   // 版权ID位: 0
    p_adts_header[3] |= (0 << 2);   // 版权ID起始: 0
    p_adts_header[3] |= ((adtsLen & 0x1800) >> 11); // 帧长度高2位

    // 字节5-6: 帧长度和缓冲区信息
    p_adts_header[4] = (uint8_t)((adtsLen & 0x7f8) >> 3); // 帧长度中8位
    p_adts_header[5] = (uint8_t)((adtsLen & 0x7) << 5);   // 帧长度低3位
    p_adts_header[5] |= 0x1f;        // 缓冲区充满度: 0x7FF(高5位)
    p_adts_header[6] = 0xfc;         // 缓冲区充满度: 0x7FF(低6位) + 结束标志

    return 0;
}

/**
 * 主函数：从MP4提取H.264和AAC裸流
 * @param argc 参数个数
 * @param argv 参数数组 [程序名, 输入文件, 输出H264, 输出AAC]
 */
int main(int argc, char **argv)
{
    // 参数验证
    if(argc != 4) {
        printf("用法: %s input.mp4 out.h264 out.aac\n", argv[0]);
        return -1;
    }

    char *in_filename = argv[1];    // 输入MP4文件
    char *h264_filename = argv[2];  // H.264输出文件
    char *aac_filename = argv[3];   // AAC输出文件
    
    // 打开输出文件
    FILE *h264_fd = fopen(h264_filename, "wb");
    if(!h264_fd) {
        perror("打开H264输出文件失败");
        return -1;
    }
    FILE *aac_fd = fopen(aac_filename, "wb");
    if(!aac_fd) {
        perror("打开AAC输出文件失败");
        fclose(h264_fd);
        return -1;
    }

    // FFmpeg相关变量
    AVFormatContext *ifmt_ctx = NULL; // 输入格式上下文
    AVBSFContext *bsf_ctx = NULL;     // 比特流过滤器上下文
    AVPacket *pkt = NULL;             // 数据包
    char errors[ERROR_STRING_SIZE];   // 错误缓冲区
    int ret = 0;
    int video_index = -1, audio_index = -1;

    // 分配输入格式上下文
    ifmt_ctx = avformat_alloc_context();
    if(!ifmt_ctx) {
        printf("分配输入上下文失败\n");
        goto cleanup;
    }

    // 打开输入文件
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL)) < 0) {
        av_strerror(ret, errors, sizeof(errors));
        printf("打开输入文件失败: %s\n", errors);
        goto cleanup;
    }

    // 获取流信息
    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_strerror(ret, errors, sizeof(errors));
        printf("获取流信息失败: %s\n", errors);
        goto cleanup;
    }

    // 查找视频/音频流索引
    video_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    audio_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (video_index < 0 || audio_index < 0) {
        printf("未找到音视频流 (video:%d, audio:%d)\n", video_index, audio_index);
        goto cleanup;
    }

    // 初始化H.264比特流过滤器 (MP4转AnnexB)
    const AVBitStreamFilter *bsfilter = av_bsf_get_by_name("h264_mp4toannexb");
    if (!bsfilter) {
        printf("获取H264过滤器失败\n");
        goto cleanup;
    }
    if ((ret = av_bsf_alloc(bsfilter, &bsf_ctx)) < 0 ||
        (ret = avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[video_index]->codecpar)) < 0 ||
        (ret = av_bsf_init(bsf_ctx)) < 0) {
        av_strerror(ret, errors, sizeof(errors));
        printf("初始化过滤器失败: %s\n", errors);
        goto cleanup;
    }

    // 分配数据包
    pkt = av_packet_alloc();
    if (!pkt) {
        printf("分配数据包失败\n");
        goto cleanup;
    }

    // 主处理循环：读取并处理数据包
    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        // 视频流处理 (H.264)
        if (pkt->stream_index == video_index) {
            // 发送原始数据包到过滤器
            if ((ret = av_bsf_send_packet(bsf_ctx, pkt)) < 0) {
                av_strerror(ret, errors, sizeof(errors));
                printf("发送视频包失败: %s\n", errors);
                av_packet_unref(pkt);
                continue;
            }

            // 接收处理后的数据包
            while (av_bsf_receive_packet(bsf_ctx, pkt) == 0) {
                fwrite(pkt->data, 1, pkt->size, h264_fd);
                av_packet_unref(pkt);
            }
        } 
        // 音频流处理 (AAC)
        else if (pkt->stream_index == audio_index) {
            AVCodecParameters *apar = ifmt_ctx->streams[audio_index]->codecpar;
            char adts_header_buf[ADTS_HEADER_LEN];
            
            // 生成ADTS头并写入
            if (adts_header(adts_header_buf, pkt->size, apar->profile, 
                           apar->sample_rate, apar->channels) == 0) {
                fwrite(adts_header_buf, 1, ADTS_HEADER_LEN, aac_fd);
            }
            
            // 写入AAC原始数据
            fwrite(pkt->data, 1, pkt->size, aac_fd);
            av_packet_unref(pkt);
        } 
        // 其他流忽略
        else {
            av_packet_unref(pkt);
        }
    }

    printf("处理完成\n");

cleanup:
    // 资源清理
    if (h264_fd) fclose(h264_fd);
    if (aac_fd) fclose(aac_fd);
    if (pkt) av_packet_free(&pkt);
    if (bsf_ctx) av_bsf_free(&bsf_ctx);
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);

    return 0;
}