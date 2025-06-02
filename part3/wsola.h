#pragma once
#include <stdint.h>

// WSOLA配置参数
typedef struct {
    int frame_size;    // 分析帧大小
    int overlap_size;  // 重叠大小
    float speed_ratio; // 变速比例
} WsolaConfig;

// WSOLA运行状态
typedef struct {
    WsolaConfig config;
    int num_channels;
    int bits;
    int byte_per_sample;
    float *curr_frame;
    float *prev_frame;
    int fout_size; // fout的大小
    float *fout;
    int input_idx;
    int output_idx;
} WsolaState;

// 初始化WSOLA状态
int wsola_state_init(WsolaState *st,
                     WsolaConfig *cfg,
                     int num_channels,
                     int bits_per_sample,
                     int max_out_frames);
// 实时处理PCM帧并输出变速后帧
int wsola_state_process(WsolaState *st,
                        const unsigned char *in_bytes,
                        int in_frames,
                        unsigned char *out_bytes,
                        int max_out_frames);
// 释放WSOLA状态资源
void wsola_state_free(WsolaState *st);
