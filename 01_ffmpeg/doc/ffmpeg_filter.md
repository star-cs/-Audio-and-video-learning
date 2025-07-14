# 1. 主要结构体 API介绍
## 1.1 AVFilterGraph 对 filters 系统的整体管理
重点变量：
- AVFilterContext **filters;
- unsigned nb_filters;

## 1.2 AVFilter 定义 filter 本身的能力
- const char *name;                // overlay
- const AVFilterPad *inputs;
- const AVFilterPad *outputs;

```c
AVFilter ff_vf_overlay = {
    .name          = "overlay",
    .description   = NULL_IF_CONFIG_SMALL("Overlay a video source on top of the input."),
    .preinit       = overlay_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(OverlayContext),
    .priv_class    = &overlay_class,
    .query_formats = query_formats,
    .activate      = activate,
    .process_command = process_command,
    .inputs        = avfilter_vf_overlay_inputs,
    .outputs       = avfilter_vf_overlay_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_SLICE_THREADS,
};
```

## 1.3 AVFilterContext filter实例，管理filter与外部的联系
- const AVFilter *filter;
- char *name;
    
- AVFilterPad *input_pads;
- AVFilterLink **inputs;
- unsigned nb_inputs
    
- AVFilterPad *output_pads;
- AVFilterLink **outputs;
- unsigned nb_outputs;
    
- struct AVFilterGraph *graph;    // 从属于哪个AVFilterGraph

## 1.4 AVFilterLink 定义两个filter之间的链接
- AVFilterContext *src;
- AVFilterPad *srcpad;
    
- AVFilterContext *dst;
- AVFilterPad *dstpad;

- struct AVFilterGraph *graph;

## 1.5 AVFilterPad 定义filter的输入/输出接口
- const char *name;
- AVFrame *(*get_video_buffer)(AVFilterLink *link, int w, int h);
- AVFrame *(*get_audio_buffer)(AVFilterLink *link, int nb_samples);

- int (*filter_frame)(AVFilterLink *link, AVFrame *frame);

- int (*request_frame)(AVFilterLink *link);

## 1.6 AVFilterInOut 过滤器输入/输出的链接列表
```c
/**
 * A linked-list of the inputs/outputs of the filter chain.
 *
 * This is mainly useful for avfilter_graph_parse() / avfilter_graph_parse2(),
 * where it is used to communicate open (unlinked) inputs and outputs from and
 * to the caller.
 * This struct specifies, per each not connected pad contained in the graph, the
 * filter context and the pad index required for establishing a link.
 */
typedef struct AVFilterInOut {
    /** unique name for this input/output in the list */
    char *name;

    /** filter context associated to this input/output */
    AVFilterContext *filter_ctx;

    /** index of the filt_ctx pad to use for linking */
    int pad_idx;

    /** next input/input in the list, NULL if this is the last */
    struct AVFilterInOut *next;
} AVFilterInOut;
```

每个AVFilter都是具有独立功能的节点，如 scale filter的作用就是进行图像尺寸变换，overlay filter的作用就是进行图像的叠加。  
特别的两个Filter，一个是buffer，一个是buffersink  
- 过滤器buffer代表filter graph的源头，原始数据就是往这个filter节点输入。
- 过滤器buffersink代表filter graph的输出节点，处理完成的数据从这个filter节点输出。

# 函数使用
//获取FFmpeg中定义的filter，调用该方法前需要先调用`avfilter_register_all();`进行滤波器注册
- AVFilter avfilter_get_by_name(const char name);

//往源滤波器buffer中输入待处理的数据
- int av_buffersrc_add_frame(AVFilterContext ctx, AVFrame frame);

//从目的滤波器buffersink中获取处理完的数据
- int av_buffersink_get_frame(AVFilterContext ctx, AVFrame frame);

// 创建一个滤波器图filter graph
- AVFilterGraph *avfilter_graph_alloc(void);

// 创建一个滤波器实例AVFilterContext,并添加到AVFilterGraph中
- int avfilter_graph_create_filter(AVFilterContext **filt_ctx, const AVFilter *filt, const char name, const char args, void *opaque, AVFilterGraph *graph_ctx);

// 连接两个滤波器节点
- int avfilter_link(AVFilterContext *src, unsigned srcpad, AVFilterContext *dst, unsigned dstpad);

# AVFilter 主体框架流程
> 比较常用的滤镜有：scale、trim、overlay、rotate、movie、yadif。scale滤镜用于缩放，trim滤镜用于帧级剪切，overlay滤镜用于视频叠加，rotate滤镜实现旋转，movie滤镜可以加载第三方的视频，yadif 滤镜可以去隔行。


# 视频过滤
[模拟水面倒影特效 示例代码](../14_filter_video.c)

1. avfilter_register_all(); 注册所有的滤镜，但是改函数已标注为过时。改为编译器自动生成AVFilter，保存在数组中
2. AVFilterGraph *filter_graph = avfilter_graph_alloc(); 创建滤镜图
3. 创建buffer source滤镜（输入源）
    ```c
        // 创建buffer source滤镜（输入源）
        char args[512];
        // 构造输入参数：视频尺寸、像素格式、时间基、宽高比
        sprintf(args, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", in_width,
                in_height, AV_PIX_FMT_YUV420P, 1, 25, 1, 1);

        const AVFilter *bufferSrc = avfilter_get_by_name("buffer");
        AVFilterContext *bufferSrc_ctx;
        ret = avfilter_graph_create_filter(&bufferSrc_ctx, bufferSrc, "in", args, NULL, filter_graph);
        if (ret < 0) {
            printf("Fail to create filter bufferSrc\n");
            return -1;
        }
    ```
4. 创建buffer sink滤镜（输出端）
    ```c
        // 创建buffer sink滤镜（输出端）
        AVBufferSinkParams *bufferSink_params;
        AVFilterContext *bufferSink_ctx;
        const AVFilter *bufferSink = avfilter_get_by_name("buffersink");
        enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE}; // 输出像素格式
        bufferSink_params = av_buffersink_params_alloc();
        bufferSink_params->pixel_fmts = pix_fmts;
        ret = avfilter_graph_create_filter(&bufferSink_ctx, bufferSink, "out", NULL, bufferSink_params,
                                        filter_graph);
        if (ret < 0) {
            printf("Fail to create filter sink filter\n");
            return -1;
        }
    ```


# 音频过滤




不想写了 ~ 先暂时跳过这里
