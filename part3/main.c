#include "main.h"

// 自定义信号处理函数
void handle_sigint(int sig) {
    printf("\n正在退出...\n");

	if (playback_thread != 0) {
		pthread_cancel(playback_thread);
		pthread_join(playback_thread, NULL);
		playback_thread = 0;
	}

	if (control_thread != 0) {
		pthread_cancel(control_thread);
		pthread_join(control_thread, NULL);
		control_thread = 0;
	}

    free_pcm_resources();
	free_mixer_resources();

    // 检查并释放音频文件资源
    if (fp != NULL) {
        fclose(fp);
        fp = NULL;
    }

    // 恢复终端模式
    disable_raw_mode();

    // 退出程序
    exit(0);
}

// 保存原始终端设置
struct termios orig_termios;

void enable_raw_mode() {
    struct termios raw;

    // 获取当前终端设置
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;

    // 禁用回显和行缓冲
    raw.c_lflag &= ~(ECHO | ICANON);

    // 设置终端属性
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

	// 隐藏光标
    printf("\033[?25l");
    fflush(stdout); //立刻生效
}

void disable_raw_mode() {
    // 恢复原始终端设置
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);

	// 显示光标
    printf("\033[?25h");
    fflush(stdout);
}


int main(int argc, char *argv [])
{	
	signal(SIGINT, handle_sigint);

	int ret;
	bool flag = true;

	// 分析文件
	//TODO 应该挪到控制线程中
	while((ret = getopt(argc,argv,"m:")) != -1){
		flag = false;
		switch(ret){
			case 'm':
				//printf("打开文件 \n");
				if(open_music_file(optarg)!=0){
					return 0;
				}
				printf("正在播放: %s \n", optarg);

				rate = wav_header.sample_rate;

				bool little_endian;
				if (strncmp(wav_header.chunk_id, "RIFF", 4) == 0) {
					little_endian = true;
				} else if (strncmp(wav_header.chunk_id, "RIFX", 4) == 0) {
					little_endian = false;
				} else {
					printf("未知文件格式\n");
					return 0;
				}

				switch (wav_header.bits_per_sample) {
                case 16:
					if(little_endian)
						pcm_format = SND_PCM_FORMAT_S16_LE;
					else
						pcm_format = SND_PCM_FORMAT_S16_BE;
                    break;
                case 24:
					if(little_endian){
						if(wav_header.block_align == wav_header.num_channels * 4){
							pcm_format = SND_PCM_FORMAT_S24_LE;
						}else if(wav_header.block_align == wav_header.num_channels * 3){
							pcm_format = SND_PCM_FORMAT_S24_3LE;
						}else{
							printf("不支持的采样位数: %d\n", wav_header.bits_per_sample);
							return 0;
						}
					}else{
						if(wav_header.block_align == wav_header.num_channels * 4){
							pcm_format = SND_PCM_FORMAT_S24_BE;
						}else if(wav_header.block_align == wav_header.num_channels * 3){
							pcm_format = SND_PCM_FORMAT_S24_3BE;
						}else{
							printf("不支持的采样位数: %d\n", wav_header.bits_per_sample);
							return 0;
						}
					}
					break;
                case 32:
					if(little_endian)
						pcm_format = SND_PCM_FORMAT_S32_LE;
					else
						pcm_format = SND_PCM_FORMAT_S32_BE;
                    break;
                default:
                    printf("不支持的采样位数: %d\n", wav_header.bits_per_sample);
                    return 0;
            	}
				break;
		}
	}


	// 初始化
	if(init_pcm() != 0){
		printf("初始化PCM失败\n"); // PCM内部的资源已经释放
		if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
		return 0;
	}

	if(init_mixer() != 0){
		printf("初始化混音器失败\n");
        free_pcm_resources();
        if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
		return 0;
	}


	enable_raw_mode(); // 启用非标准模式
	pthread_create(&playback_thread, NULL, playback_thread_func, NULL);
	pthread_create(&control_thread, NULL, control_thread_func, NULL);

	pthread_join(control_thread, NULL); //阻塞等待
	pthread_join(playback_thread, NULL); // 阻塞等待
	playback_thread = 0; // 播放线程退出
	control_thread = 0; // 音量控制线程退出
	disable_raw_mode(); // 恢复标准模式

	printf("\n结束\n");

	free_pcm_resources();
	free_mixer_resources();
	if (fp != NULL) {
        fclose(fp);
        fp = NULL;
    }
	signal(SIGINT, SIG_DFL);
	return 0;
}

int open_music_file(const char *path_name){

	// 打开音频文件，输出其文件头信息

	fp = fopen(path_name, "rb");
	if(fp == NULL){
		printf("打开文件失败: %s \n", strerror(errno));
		return 1;
	}

	if (strcmp(path_name + strlen(path_name) - 4, ".wav") != 0) {
        printf("文件必须为wav文件\n");
        return 1;
    }

	// 读取文件头
	fread(&wav_header, sizeof(struct WAV_HEADER), 1, fp);

	// 打印文件头信息
	/*printf("wav文件头结构体大小: %d\n", (int)sizeof(wav_header));
    printf("RIFF标识: %.4s\n", wav_header.chunk_id);
    printf("文件大小: %d\n", wav_header.chunk_size);
    printf("文件格式: %.4s\n", wav_header.format);
    printf("格式块标识: %.4s\n", wav_header.sub_chunk1_id);
    printf("格式块长度: %d\n", wav_header.sub_chunk1_size);
    printf("编码格式代码: %d\n", wav_header.audio_format);
    printf("声道数: %d\n", wav_header.num_channels);
    printf("采样频率: %d\n", wav_header.sample_rate);
    printf("传输速率: %d\n", wav_header.byte_rate);
    printf("数据块对齐单位: %d\n", wav_header.block_align);
    printf("采样位数(长度): %d\n", wav_header.bits_per_sample);
    printf("数据块标识: %.4s\n", wav_header.sub_chunk2_id);
    printf("数据块长度: %d\n", wav_header.sub_chunk2_size);*/

	return 0;
}

