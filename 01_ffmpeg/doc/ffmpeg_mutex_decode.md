# 1. 解复用
[解复用，分析音频视频格式代码](../01_ffmpeg_demux.c)
## 1.1 流程
1. `avformat_open_input` 创建 AVFormatContext*，打开媒体文件，仅能获取基础信息（如流数据/粗略格式）(https://blog.csdn.net/leixiaohua1020/article/details/44064715)
2. `avformat_find_stream_info`，会进一步读取并分析音视频数据包填充以下详细信息：视频流（编码格式，分辨率，帧率，比特率，GOP）；音频流（编码格式，采样率，声道数，采样格式(FLTP/S16)；全局信息：文件总时长(duration)，平均码率(bit_rate) ）。`重点，此时会把数据包缓存到 AVFormatContext，后续通过av_read_frame()读出。 `(https://blog.csdn.net/leixiaohua1020/article/details/44084321)(源码挺复杂的，TODO了解)
3. `av_dump_format` 打印流参数出来
4. 遍历AVFormatContext里的streams得到每个AVStream*
5. 通过AVStream里的(`AVCodecParamters*` 专注于存储与编码数据本身相关的静态属性)codecpar->codec_type判断是否是音视频流
6. AVSteam里的AVCodecParamters结构体codecpar，可以得到
codec_type，sample_rate（采样率），format（音频格式），
channels（音频信道数量），codec_id（压缩编码格式），width，heigth（和AVCodecContext里的静态信息差不多）
6. 通过`int av_read_frame(AVFormatContext *s, AVPacket *pkt);`
获取到 AVPacket* pkt 信息 `（备注：这里的AVPacket和AVFormatContext内缓存AVPacketLists节点 共同引用计数）`
(https://blog.csdn.net/leixiaohua1020/article/details/12678577)
7. 打印出 AVPacket 里的基础信息：pts，dts，size，pos



# 2. 提取 AAC 
[解码，提取AAC](../02_extract_aac.c)

## 2.1. AAC格式
MPEG-4 标准定义的有损压缩格式
- `ADIF`。音频数据交换格式。特征：可以确定地找到这个音频数据的开始，不能进行在音频数据流中间开始的解码，即它的解码必须在明确定义的开始进行。`常用于磁盘文件中。`
- `ADTS`。AAC 音频的传输流格式。有同步字的比特流，解码可以在这个流中任意位置开始。它的特征类似于 mp3 的数据流格式。
![](../../img/aac.png)

### 2.1.1. 固定头信息 adts_fixed_header（7 字节）
![](../../img/adts_fixed_header.png)
- syncword：同步头，总是 0xFFF，代表一个 ADTS 帧的开始
- ID：MPEG 标识符，0 标识 MPEG-4，1 标识 MPEG-2
- Layer：always：'00'
- protection_absent：是否误码校验。0 表示 增加校验，增加 adts_variable_header；0 反之
- profile：表示使用哪个级别的 AAC。MPEG-2 / MPEG-4
    - MPEG-2
    - MPEG-4
- sampling_frequency_index：表示使用的采样率下标，通过下标在 Sampling Frequencies[] 数组中查找得到采样率的值。（4bit）
- channel_configuration：表示声道数。比如 2 表示立体声双声道

### 2.1.2 可变字段  adts_variable_header
![](../../img/adts_variable_header.png)
- aac_frame_length：一个 ADTS 帧的长度包括 ADTS 头和 AAC 原始流。
> aac_frame_length = (protection_absent == 1 ? 7 : 9) + size(AACFrame)
- adts_buffer_fullness：0x7FF 说明码率可变的码流。
- number_of_raw_data_blocks_in_frame：表示 ADTS 帧中有 number_of_raw_data_blocks_in_frame + 1 个 AAC 原始帧。例如 number_of_raw_data_blocks_in_frame == 0 表示 ADTS 帧中有一个 AAC 数据块。（2bit）


## 2.2 流程
1. `avformat_open_input`
2. `avformat_find_stream_info`
3. `av_dump_format`

4. `av_find_best_stream` 
> a. 就是要获取音视频及字幕的stream_index  
> b.以前没有函数av_find_best_stream时，获取索引可以通过遍历（参考解复用代码）  
- 返回的是int值，就是返回音视频的索引值
- ic是AVFormatContext，从avformat_open_input中得来
- type是AVMediaType，是类型，在我其他文章中有提到，比如要获取视频流那么这个就是AVMEDIA_TYPE_VIDEO
- wanted_stream_nb让他自己选择填入1
- related_stream是关联流，基本不使用也填写-1
- flags填入0
```c
int av_find_best_stream(AVFormatContext *ic,
                        enum AVMediaType type,
                        int wanted_stream_nb,
                        int related_stream,
                        AVCodec **decoder_ret,
                        int flags);
```

5. 得到音频流后，查找 AVFormatContext 里的 AVStream** 。得到音频流 AVStream*，进一步可以得到 AVStream里的 `AVCodecParameters * codecpar`（和解复用差不多，通过 AVCodecParameters 获取到规格信息）

6. `av_init_packet(AVPacket*)`    初始化AVPacket
7. `av_read_frame` 从 AVFormaatContext AVPacketLists 读取缓存
8. `adts_header` 自定义函数，生成AAC格式中的adts header，一共7字节。详见代码

9. `av_packet_unref(AVPacket*)` 解引用
10. `avformat_close_input` 关闭 AVFormatContext

# 3. 提取 h264
[解码，提取h264](../03_extract_h264.c)

## 3.1. h264格式
H264除了实现对视频的压缩处理之外，为了方便网络传输，提供对应的视频编码和分片策略；类似于网络数据封装成IP帧，在H264中被称为组（GOP），片（slice），宏块（Macroblock）一起组成了H264的码流分层结构；  
重点：GOP主要用作形容一个IDR帧到下一个IDR帧之间的间隔了多少个帧。

### 3.1.1. IDR 帧
一个序列的第一个图像叫做IDR图像（立即刷新图像），IDR图像都是I帧图像。  
I 和 IDR帧都使用帧内预测。I 帧不用参考任何帧，但是之后的P，B帧可能参考这个I帧之前的帧，但是 IDR 帧不允许。   
`核心作用`：为了解码的重同步，当解码器解码到 IDR 图像时，可以讲缓存帧队列清空。当前一个序列出现错误，可以重新同步。  
> IDR 帧一定是 I 帧，I 帧不一定是 IDR 帧 ⭐

### 3.1.2. NALU
![](../../img/h264_nalu.png)
> 解码器需要一开始收到 SPS，PPS 进行初始化 ⭐    
> 发送 I 帧之前， 至少发一次 SPS 和 PPS 

H264原始码流是由多个NALU组成，分为两层：VCL（视频编码层）和 NAL（网络提取层）：（感觉不重要~）
- VCL：包括核心压缩引擎和块，宏块和片的语法级别定义，设计目标是尽可能地独立于网络进行高
效的编码;
- NAL：负责将VCL产生的比特字符串适配到各种各样的网络和多元环境中，覆盖了所有片级以上的语法级别

> VCL --> NAL（传输单元）  
> 在 VCL 进行数据传输或存储之前，这些编码的 VCL 数据，被映射或封装进 NAL 单元。  
> 一个 NALU = 一组对应于视频编码的 NALU 头部信息 + 一个原始字节序列负荷（RBSP， Raw Byte Sequence Payload）。

![](../../img/h264_nalu_base.png)
- 每一个 NALU ，可以通过 0x000001 或 0x00000001，用来指示一个 NALU 的起始和终止位置  
- 3 字节的 0x000001 只有一种情况：一个完整帧被编码为多个Slice时​
​首Slice​：使用4字节起始码 0x00000001，标识该帧的开始（即Access Unit的首个NALU）。​后续Slice​：使用3字节起始码 0x000001，标记同一帧内其他Slice的NALU起始位置。
> 分3字节/4字节起始码的好处：节省体积，支持随机访问（4字节起始码标记帧/关键数据的起点，使解码器可快速跳转至指定帧）

> 对于 解复用，MP4文件读取出来的packet不带startcode，但是TS文件读取出来的packet带startcode ⭐详见代码
#### 3.1.2.1. NALU header
![](../../img/h264_nalu_header.png)

### 3.1.3. H264封装模式
H264 两种封装
- 一种是 annexb 模式 ，传统模式，有 startcode，SPS 和 PPS 在 ES 中。一般分离h265转换为这种。
- 一种是 mp4 模式，一般 mp4 mkv 都是 mp4 模式，没有 startcode，SPS 和 PPS 以及其他信息被封装在 container 中，每一个 frame 前面 4 个字节是这个 frame 的长度   

> 很多解码器只支持 annexb 这种模式，因此需要将 mp4 做转换：在 ffmpeg 中使用 `h264_mp4toannexb_filter` 即可  
> h265_mp4toannexb_filter 可以判别格式。例如 ts 流不会处理。


## 3.3. 流程
1. avformat_alloc_context
2. avformat_open_input
3. avformat_find_stream_info
4. `videoindex = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);` 找视频流index
5. AVPacket * pkt = av_packet_alloc();  av_init_packet(pkt);
6. `const AVBitStreamFilter *bsfilter = av_bsf_get_by_name("h264_mp4toannexb");` 关键，转换mp4格式H264 为 Annex B格式。⭐
7. `AVBSFContext *bsf_ctx = NULL; av_bsf_alloc(bsfilter, &bsf_ctx);` 创建过滤器上下文。
8. `avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[videoindex]->codecpar); av_bsf_init(bsf_ctx);` 复制视频编解码参数到过滤器，初始化过滤器。

9. av_read_frame
10. 处理细节：MP4需要经过`AVBSFContext *bsf_ctx`过滤器转换生成 startcode；ts格式已包含起始码，可直接写入。（重点⭐）


# 4. flv bit流解析
## 4.1 flv格式

### 4.1.1 flv header


### 4.1.2 flv body

### 4.1.2.1 flv tag

#### a. tag header

#### b. Script Tag Data

#### c. Audio Tag Data

##### AAC Audio Data

#### d. Video Tag Data


# 5. 解码 AAC

# 6. 解码 H264

# 7. FFmpeg 解码 MP4
## 7.1 MP4 格式

## 7.2 解码流程

# 8. AVIO 内存输入模式

# 9. 音频重采样

