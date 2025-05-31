#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "wsola.h"

// 辅助：PCM 转浮点
static float read_sample(const unsigned char *data, int bits, int channel, int offset, int num_channels) {
    int byte_per_sample = bits / 8;
    int idx = offset * byte_per_sample * num_channels + channel * byte_per_sample;
    float sample = 0.0f;
    if (bits == 16) {
        int16_t s = *(int16_t*)(data + idx);
        sample = (float)s / 32768.0f;
    } else if (bits == 24) {
        // 24位小端序，符号位扩展到32位
        int32_t s = ((int32_t)(data[idx+2] << 16)) | ((int32_t)(data[idx+1] << 8)) | (int32_t)data[idx];
        sample = (float)s / 8388608.0f;  // 2^23
    } else if (bits == 32) {
        int32_t s = *(int32_t*)(data + idx);
        sample = (float)s / 2147483648.0f;  // 2^31
    }
    return sample;
}

// 辅助：浮点 转 PCM
static void write_sample(unsigned char *data, float sample, int bits, int channel, int offset, int num_channels) {
    int byte_per_sample = bits / 8;
    int idx = offset * byte_per_sample * num_channels + channel * byte_per_sample;
    if (bits == 16) {
        int16_t s = (int16_t)(sample * 32767.0f);
        *(int16_t*)(data + idx) = s;
    } else if (bits == 24) {
        int32_t s = (int32_t)(sample * 8388607.0f);
        data[idx] = (unsigned char)s;
        data[idx+1] = (unsigned char)(s >> 8);
        data[idx+2] = (unsigned char)(s >> 16);
    } else if (bits == 32) {
        int32_t s = (int32_t)(sample * 2147483647.0f);
        *(int32_t*)(data + idx) = s;
    }
}

// 辅助：寻找最佳重叠偏移
static int find_best_offset(const float *curr, const float *prev, int frame_size, int overlap_size) {
    int best_offset = 0;
    float max_corr = -INFINITY;
    for (int offset = 0; offset < overlap_size; offset++) {
        float corr = 0.0f;
        for (int i = 0; i < overlap_size; i++) {
            corr += curr[i] * prev[i + offset];
        }
        if (corr > max_corr) {
            max_corr = corr;
            best_offset = offset;
        }
    }
    return best_offset;
}

// 初始化 WSOLA 状态（由 play.c 在打开文件、读取 WAV header 后调用）
int wsola_state_init(WsolaState *st,
                     WsolaConfig *cfg,
                     int num_channels,
                     int bits_per_sample)
{
    *st = (WsolaState){ .config = *cfg,
                        .num_channels = num_channels,
                        .bits = bits_per_sample,
                        .byte_per_sample = bits_per_sample / 8,
                        .input_idx = 0,
                        .output_idx = 0 };
    st->curr_frame = malloc(cfg->frame_size * num_channels * sizeof(float));
    st->prev_frame = malloc(cfg->frame_size * num_channels * sizeof(float));
    if (!st->curr_frame || !st->prev_frame) return -1;
    // 先填一个全零的“前一帧”
    memset(st->prev_frame, 0, cfg->frame_size * num_channels * sizeof(float));
    return 0;
}

// 实时处理：输入一批 PCM 字节，输出一批 PCM 字节
// in_bytes        - 输入原始 PCM 缓冲区
// in_frames       - 输入帧数（注意 interleaved）
// out_bytes       - 输出 PCM 缓冲区
// max_out_frames  - 输出缓冲区最多能容纳的帧数
// 返回实际输出帧数
int wsola_state_process(WsolaState *st,
                        const unsigned char *in_bytes,
                        int in_frames,
                        unsigned char *out_bytes,
                        int max_out_frames)
{
    if (st->config.speed_ratio == 1)
        return in_frames; // 速率为1时不处理
    st->input_idx = 0;  // 重置输入索引
    st->output_idx = 0; // 重置输出索引
    //printf("WSOLA speed_ratio: %f\n", st->config.speed_ratio);
    int nc = st->num_channels;
    int bps = st->bits;
    int chunk = st->config.frame_size - st->config.overlap_size;
    //fprintf(stderr, "WSOLA process in_frames=%d, chunk=%d, max_out_frames=%d\n",
    //    in_frames, chunk, max_out_frames);
    // 清浮点输出缓冲
    float *fout = calloc(max_out_frames * nc, sizeof(float));
    int out_written = 0;

    // 将 in_bytes 拆成浮点帧，迭代处理
    int processes = in_frames / chunk;
    for (int p = 0; p < processes && out_written + chunk <= max_out_frames; p++) {
        // 1) 读 chunk 样本到 curr_frame（多通道交错）
        for (int c = 0; c < nc; c++)
            for (int i = 0; i < st->config.frame_size; i++) {
                int idx = (p*chunk + i)*nc + c;
                if (idx < in_frames*nc) {
                    st->curr_frame[i*nc + c] =
                      read_sample(in_bytes, bps, c, p*chunk + i, nc);
                } else {
                    st->curr_frame[i*nc + c] = 0;
                }
            }
        // 2) 计算偏移并叠加
        int offset = find_best_offset(st->curr_frame, st->prev_frame,
                                      st->config.frame_size,
                                      st->config.overlap_size);
        int ostart = (int)(st->output_idx / st->config.speed_ratio);
        for (int c = 0; c < nc; c++)
            for (int i = 0; i < chunk; i++) {
                int op = (ostart + i)*nc + c;
                if (op >= max_out_frames*nc) continue;
                int cp = i*nc + c;
                int pp = (offset + i)*nc + c;
                fout[op] += st->curr_frame[cp];
                if (ostart >= st->config.overlap_size)
                    fout[op] += st->prev_frame[pp];
            }
        // 3) 更新状态
        memcpy(st->prev_frame, st->curr_frame, st->config.frame_size*nc*sizeof(float));
        st->input_idx  += chunk;
        st->output_idx += chunk;
        out_written = ostart + chunk;
    }

    // 迭代叠加之后
    if (out_written > max_out_frames) {
        fprintf(stderr, "WSOLA warning: out_written(%d) > max(%d), clamp\n",
                out_written, max_out_frames);
        out_written = max_out_frames;
    }

    // 4) 把浮点 f_out 转成 PCM
    for (int f = 0; f < out_written; f++)
        for (int c = 0; c < nc; c++)
            write_sample(out_bytes, fout[f*nc + c], bps, c, f, nc);

    free(fout);

    if (out_written <= 0) {
        fprintf(stderr, "WSOLA: no frames generated, skipping write\n");
        return 0;
    }
    return out_written;
}

// 释放
void wsola_state_free(WsolaState *st) {
    free(st->curr_frame);
    free(st->prev_frame);
}