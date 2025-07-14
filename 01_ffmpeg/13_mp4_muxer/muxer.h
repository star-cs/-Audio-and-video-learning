#pragma once

#include <iostream>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/mathematics.h"
#include "libavutil/rational.h"
}

class Muxer
{
public:
    Muxer();
    ~Muxer();

    /**
    输出文件，返回 <0 值异常
    初始化
    */
    int Init(const char *url);

    // 资源释放
    void DeInit();

    // 创建流，外部设置好AVCodecContext参数传入
    int AddStream(AVCodecContext *codec_ctx);

    // 写流
    int SendHeader();
    int SendPacket(AVPacket *packet);
    int SendTrailer();

    int Open(); // avio_open

    int GetAudioStreamIndex() { return audio_index_; }
    int GetVideoStreamIndex() { return video_index_; }

private:
    AVFormatContext *fmt_ctx_ = NULL;
    std::string url_ = "";

    // 编码器上下文
    AVCodecContext *aud_codec_ctx_ = NULL;
    AVStream *aud_stream_ = NULL;
    AVCodecContext *vid_codec_ctx_ = NULL;
    AVStream *vid_stream_ = NULL;

    int audio_index_ = -1;
    int video_index_ = -1;
};