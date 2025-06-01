#include "main.h"

int track_index = 0; // 当前播放的曲目索引
char *wav_files[MAX_WAV_FILES];
int wav_file_count = 0;

// 初始化采样率
unsigned int rate;

// 缓存大小
#define BUF_LEN 1024
//char buf[BUF_LEN];
unsigned char *buff = NULL;

// 周期数
int periods = 2;
// 一个周期的大小，这里虽然是设置的字节大小，但是在有时候需要将此大小转换为帧，所以在用的时候要换算成帧数大小才可以
snd_pcm_uframes_t period_size = 12 * 1024;
snd_pcm_uframes_t frames;
snd_pcm_uframes_t buffer_size;

/**
	ALSA的变量定义
**/
// 定义用于PCM流和硬件的 
snd_pcm_hw_params_t *hw_params = NULL;
// PCM设备的句柄  想要操作PCM设备必须定义
snd_pcm_t *pcm_handle = NULL;
// 定义pcm的name snd_pcm_open函数会用到,strdup可以直接把要复制的内容复制给没有初始化的指针，因为它会自动分配空间给目的指针，需要手动free()进行内存回收。
char *pcm_name = NULL;
// 定义是播放还是回放等等，播放流 snd_pcm_open函数会用到 可以在 https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html#gac23b43ff55add78638e503b9cc892c24 查看
snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
// 定义采样位数
snd_pcm_format_t pcm_format;

struct WAV_HEADER wav_header;
int wav_header_size; 

// 音乐文件指针变量
FILE *fp = NULL;

pthread_t playback_thread = 0;
pthread_t control_thread = 0; // 播放线程和音量控制线程
pthread_t ui_thread = 0; // UI线程

long played_bytes = 0; // 已播放的字节数

snd_mixer_t *mixer_handle = NULL;
snd_mixer_elem_t *elem = NULL;
snd_mixer_selem_id_t *sid = NULL;
long current_volume = 50; // 当前音量
long init_volume = 50; // 初始音量

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

bool pause_flag = false; // 暂停标志
bool exit_flag = false; // 退出标志
bool finish_flag = false; // 播放完成标志
bool error_flag = false; // 错误标志
float playback_speed = 1.0;

long data_offset_in_file = 0; // data块在文件中的偏移量
WsolaConfig ws_cfg;
WsolaState ws_state = {0}; // Wsola状态