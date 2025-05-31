#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <termios.h>
#include <signal.h>
#include <stddef.h>
#include <errno.h>
#include "log.h"

#define MAX_WAV_FILES 256
#define MAX_FILENAME_LEN 256


// 定义音乐全局结构体，参考 https://www.cnblogs.com/ranson7zop/p/7657874.html 表3
// int 由uint32_t代替，short 由uint16_t代替，因为在跨平台后有可能不兼容，类型长度不一致，使用统一的类型
struct WAV_HEADER
{

	char 		chunk_id[4]; // riff 标志号
	uint32_t 	chunk_size; // riff长度
	char 		format[4]; // 格式类型(wav)
	
	char 		sub_chunk1_id[4]; // fmt 格式块标识
	uint32_t 	sub_chunk1_size; // fmt 长度 格式块长度。
	uint16_t 	audio_format; // 编码格式代码									常见的 WAV 文件使用 PCM 脉冲编码调制格式,该数值通常为 1
	uint16_t 	num_channels; // 声道数 									单声道为 1,立体声或双声道为 2
	uint32_t  	sample_rate; // 采样频率 									每个声道单位时间采样次数。常用的采样频率有 11025, 22050 和 44100 kHz。
	uint32_t 	byte_rate; // 传输速率 										该数值为:声道数×采样频率×每样本的数据位数/8。播放软件利用此值可以估计缓冲区的大小。
	uint16_t	block_align; // 数据块对齐单位									采样帧大小。该数值为:声道数×位数/8。播放软件需要一次处理多个该值大小的字节数据,用该数值调整缓冲区。
	uint16_t 	bits_per_sample; // 采样位数								存储每个采样值所用的二进制数位数。常见的位数有 4、8、12、16、24、32

	char 		sub_chunk2_id[4]; // 数据  不知道什么数据
	uint32_t 	sub_chunk2_size; // 数据大小
	
};
extern struct WAV_HEADER wav_header; // 定义wav_header结构体变量
extern int wav_header_size; // 接收wav_header数据结构体的大小


extern int track_index; // 当前播放的曲目索引
extern char *wav_files[]; // wav文件名数组
extern int wav_file_count; // 当前wav文件数量

/**
	ALSA的变量定义
**/
// 定义用于PCM流和硬件的 
extern snd_pcm_hw_params_t *hw_params;
// PCM设备的句柄  想要操作PCM设备必须定义
extern snd_pcm_t *pcm_handle;
// 定义pcm的name snd_pcm_open函数会用到,strdup可以直接把要复制的内容复制给没有初始化的指针，因为它会自动分配空间给目的指针，需要手动free()进行内存回收。
extern char *pcm_name;
// 定义是播放还是回放等等，播放流 snd_pcm_open函数会用到 可以在 https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#gac23b43ff55add78638e503b9cc892c24 查看
extern snd_pcm_stream_t stream;
// 定义采样位数
extern snd_pcm_format_t pcm_format;


// 缓存大小
// #define BUF_LEN 1024
//char buf[BUF_LEN];
extern unsigned char *buff;


// 周期数
extern int periods;
// 一个周期的大小，这里虽然是设置的字节大小，但是在有时候需要将此大小转换为帧，所以在用的时候要换算成帧数大小才可以
extern snd_pcm_uframes_t period_size;
extern snd_pcm_uframes_t frames;
extern snd_pcm_uframes_t buffer_size;





// 初始化采样率
extern unsigned int rate;

// 音乐文件指针变量
extern FILE *fp;


// 函数声明
int open_music_file(const char *path_name);
void handle_sigint(int sig);
void *playback_thread_func(void *arg);
void *control_thread_func(void *arg);
void *ui_thread_func(void *arg);
void enable_raw_mode();
void disable_raw_mode();
int init_pcm();
int init_mixer();
void free_pcm_resources();
void free_mixer_resources();

extern pthread_t playback_thread;
extern pthread_t control_thread; // 播放线程和音量控制线程
extern pthread_t ui_thread; // UI线程

extern snd_mixer_t *mixer_handle;
extern snd_mixer_elem_t *elem;
extern snd_mixer_selem_id_t *sid;
extern long init_volume; // 初始音量, 初始化mixer用
extern long current_volume; // 记录用户操作的音量

extern long played_bytes; // 已播放的字节数

// 线程通讯
extern bool pause_flag; // 暂停标志
extern bool exit_flag; // 退出标志
extern bool finish_flag; // 播放完成标志
extern bool error_flag; // 错误标志
extern pthread_mutex_t mutex;
extern pthread_cond_t cond;

extern long data_offset_in_file; // data块在文件中的偏移量