#pragma once

#include <vector>
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
}

class AudioEncoder
{
public:
    AudioEncoder();
    ~AudioEncoder();
    int InitAAC(int channels, int sample_rate, int bit_rate);
    //    int InitMP3(/*int channels, int sample_rate, int bit_rate*/);
    // 释放资源
    void DeInit();
    AVPacket *Encode(AVFrame *frame, int stream_index, int64_t pts, int64_t time_base);
    int Encode(AVFrame *frame, int stream_index, int64_t pts, int64_t time_base, std::vector<AVPacket*>& packets);
    // 获取一帧数据 每个通道需要多少个采样点
    int GetFrameSize();
    // 编码器需要的采样格式
    int GetSampleFormat();
    AVCodecContext *GetCodecContext();
    int GetChannels() { return channels_; }
    int GetSampleRate() { return sample_rate_; }

private:
    int channels_ = 2;
    int sample_rate_ = 44100;
    int bit_rate_ = 128 * 1024;
    int64_t pts_ = 0;
    AVCodecContext *codec_ctx_ = NULL;
};