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
#include "const.h"

// 自定义信号处理函数
void handle_sigint(int sig) {
    printf("\n正在退出...\n");

	if (playback_thread != 0) {
		pthread_cancel(playback_thread);
		pthread_join(playback_thread, NULL);
		playback_thread = 0;
		printf("播放线程已取消并退出。\n");
	}

	if (volume_thread != 0) {
		pthread_cancel(volume_thread);
		pthread_join(volume_thread, NULL);
		volume_thread = 0;
		printf("音量控制线程已取消并退出。\n");
	}

    // 检查并释放音频文件资源
    if (fp != NULL) {
        fclose(fp);
        fp = NULL;
        printf("音频文件已关闭。\n");
    }

    // 检查并释放 PCM 资源
    if (pcm_handle != NULL) {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
        printf("PCM 设备已关闭。\n");
    }

    // 检查并释放混音器资源
    if (mixer_handle != NULL) {
        snd_mixer_close(mixer_handle);
        mixer_handle = NULL;
        printf("混音器已关闭。\n");
    }

    // 检查并释放混音器控件 ID
    if (sid != NULL) {
        snd_mixer_selem_id_free(sid);
        sid = NULL;
        printf("混音器控件 ID 已释放。\n");
    }

    // 检查并释放 PCM 硬件参数
    if (hw_params != NULL) {
        snd_pcm_hw_params_free(hw_params);
        hw_params = NULL;
        printf("PCM 硬件参数已释放。\n");
    }

    // 检查并释放 PCM 名称
    if (pcm_name != NULL) {
        free(pcm_name);
        pcm_name = NULL;
        printf("PCM 名称已释放。\n");
    }

    // 检查并释放缓冲区
    if (buff != NULL) {
        free(buff);
        buff = NULL;
        printf("缓冲区已释放。\n");
    }

    // 恢复终端模式
    disable_raw_mode();
    printf("终端模式已恢复。\n");

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

int init_mixer() {
    if (snd_mixer_open(&mixer_handle, 0) < 0) {
        printf("无法打开混音器\n");
        return 1;
    }

    if (snd_mixer_attach(mixer_handle, "default") < 0) {
        printf("无法附加混音器\n");
        snd_mixer_close(mixer_handle);
        return 1;
    }

    if (snd_mixer_selem_register(mixer_handle, NULL, NULL) < 0) {
        printf("无法注册混音器控件\n");
        snd_mixer_close(mixer_handle);
        return 1;
    }

    if (snd_mixer_load(mixer_handle) < 0) {
        printf("无法加载混音器控件\n");
        snd_mixer_close(mixer_handle);
        return 1;
    }

    if (snd_mixer_selem_id_malloc(&sid) < 0) {
        printf("无法分配控件 ID\n");
        snd_mixer_close(mixer_handle);
        return 1;
    }

    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

    elem = snd_mixer_find_selem(mixer_handle, sid);
    if (!elem) {
        printf("无法找到 Master 控件\n");
        snd_mixer_selem_id_free(sid); // 释放 sid
        snd_mixer_close(mixer_handle); // 关闭 mixer_handle
        return 1;
    }

    // 设置初始音量
    long min, max;
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    long set_volume = min + (init_volume * (max - min) / 100);
    snd_mixer_selem_set_playback_volume_all(elem, set_volume);

    snd_mixer_selem_id_free(sid); // 释放 sid
    return 0;
}

int init_pcm() {
    if (snd_pcm_hw_params_malloc(&hw_params) < 0) {
        printf("分配snd_pcm_hw_params_t结构体失败\n");
        return 1;
    }

    pcm_name = strdup("default");
    if (snd_pcm_open(&pcm_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        printf("打开PCM设备失败\n");
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    if (snd_pcm_hw_params_any(pcm_handle, hw_params) < 0) {
        printf("初始化配置空间失败\n");
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    if (snd_pcm_hw_params_test_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        printf("测试交错模式失败\n");
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    if (snd_pcm_hw_params_set_format(pcm_handle, hw_params, pcm_format) < 0) {
        printf("设置样本长度失败\n");
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    unsigned int exact_rate = rate;
    if (snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &exact_rate, 0) < 0) {
        printf("设置采样率失败\n");
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    if (snd_pcm_hw_params_set_channels(pcm_handle, hw_params, wav_header.num_channels) < 0) {
        printf("设置通道数失败\n");
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    buffer_size = period_size * periods;
    buff = (unsigned char *)malloc(buffer_size);
    if (!buff) {
        printf("分配缓冲区失败\n");
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

    // 根据采样位数设置缓冲区大小
    if (wav_header.bits_per_sample == 16) {
        frames = buffer_size >> 2;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S16_LE OR S16_BE缓冲区失败\n");
            free(buff); // 释放 buff
            snd_pcm_close(pcm_handle); // 关闭 PCM 设备
            snd_pcm_hw_params_free(hw_params); // 释放 hw_params
            free(pcm_name); // 释放 pcm_name
            return 1;
        }
    } else if (wav_header.bits_per_sample == 24 && wav_header.block_align == wav_header.num_channels * 3) {
        frames = buffer_size / 6;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S24_3LE OR S24_3BE缓冲区失败\n");
            free(buff); // 释放 buff
            snd_pcm_close(pcm_handle); // 关闭 PCM 设备
            snd_pcm_hw_params_free(hw_params); // 释放 hw_params
            free(pcm_name); // 释放 pcm_name
            return 1;
        }
    } else if (wav_header.bits_per_sample == 32 || (wav_header.bits_per_sample == 24 && wav_header.block_align == wav_header.num_channels * 4)) {
        frames = buffer_size >> 3;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S32_LE OR S32_BE OR S24_LE OR S24_BE缓冲区失败\n");
            free(buff); // 释放 buff
            snd_pcm_close(pcm_handle); // 关闭 PCM 设备
            snd_pcm_hw_params_free(hw_params); // 释放 hw_params
            free(pcm_name); // 释放 pcm_name
            return 1;
        }
    }

    if (snd_pcm_hw_params(pcm_handle, hw_params) < 0) {
        printf("设置的硬件配置参数失败\n");
        free(buff); // 释放 buff
        snd_pcm_close(pcm_handle); // 关闭 PCM 设备
        snd_pcm_hw_params_free(hw_params); // 释放 hw_params
        free(pcm_name); // 释放 pcm_name
        return 1;
    }

	free(pcm_name);
    snd_pcm_hw_params_free(hw_params); // 释放 hw_params
    return 0;
}

void *volume_control_thread(void *arg) {

    int volume = init_volume; // 初始音量值
    const int max_volume = 100;
    const int min_volume = 0;
	
	printf("\n");
	printf("使用左方向键减少音量，右方向键增加音量\n");

    while (1) {
        // 显示音量进度条
        printf("\r音量: [");
        for (int i = 0; i < max_volume / 2; i++) {
            if (i < volume / 2) {
                printf("#"); // 填充的部分
            } else {
                printf(" "); // 未填充的部分
            }
        }
        printf("] %3d%%", volume);
		fflush(stdout);

        // 捕获用户输入
        char input = getchar();
        if (input == '\033') { // 检测到转义序列
            getchar();         // 跳过 '['
            input = getchar(); // 获取方向键
            if (input == 'D') { // 左方向键
                if (volume > min_volume) {
                    volume -= 5; // 减小音量
                }
            } else if (input == 'C') { // 右方向键
                if (volume < max_volume) {
                    volume += 5; // 增大音量
                }
            }
        }

		if (volume < min_volume) volume = min_volume;
		if (volume > max_volume) volume = max_volume;

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
        long min, max;
		snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
		long set_volume = min + (volume * (max - min) / 100);
		snd_mixer_selem_set_playback_volume_all(elem, set_volume);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    return NULL;
}

void *playback_thread_func(void *arg) {

	int ret;
	// 跳过 WAV 文件头
	fseek(fp, sizeof(struct WAV_HEADER), SEEK_SET);
	// feof函数检测文件结束符，结束：非0, 没结束：0 !feof(fp)
	while(1){
		// 读取文件数据放到缓存中
		ret = fread(buff, 1, buffer_size, fp);
		
		if(ret == 0){
			
			// 把输出放到主函数, 避免输出和音量线程干扰
			break;
		}
		
		if(ret < 0){
			
			printf("\n文件读取错误: %s \n", strerror(errno));
			break;
		}

		// 向PCM设备写入数据,
		while((ret = snd_pcm_writei(pcm_handle, buff, frames)) < 0){
			if (ret == -EPIPE){

				/* EPIPE means underrun -32  的错误就是缓存中的数据不够 */
				printf("\nunderrun occurred -32, err_info = %s \n", snd_strerror(ret));
				//完成硬件参数设置，使设备准备好
				snd_pcm_prepare(pcm_handle);
			
            } else if(ret < 0){
				printf("\nret value is : %d \n", ret);
				printf("\nwrite to audio interface failed: %s \n", snd_strerror(ret));

				if (volume_thread != 0) {
					pthread_cancel(volume_thread);
					pthread_join(volume_thread, NULL);
					volume_thread = 0;
					printf("音量控制线程已取消并退出。\n");
				}

				// 释放资源并退出
				if (fp != NULL) {
					fclose(fp);
					fp = NULL;
					printf("音频文件已关闭。\n");
				}

				if (buff != NULL) {
					free(buff);
					buff = NULL;
					printf("缓冲区已释放。\n");
				}

				if (pcm_handle != NULL) {
					snd_pcm_close(pcm_handle);
					pcm_handle = NULL;
					printf("PCM 设备已关闭。\n");
				}

				if (mixer_handle != NULL) {
					snd_mixer_close(mixer_handle);
					mixer_handle = NULL;
					printf("混音器已关闭。\n");
				}

				// 恢复终端模式
				disable_raw_mode();
				printf("终端模式已恢复。\n");

				// 退出程序
				exit(EXIT_FAILURE); // 使用 EXIT_FAILURE 表示程序异常退出
			}
		}
	}
}


int main(int argc, char *argv [])
{
	int ret;
	bool flag = true;

	// 分析文件
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
		fclose(fp);
		return 0;
	}

	if(init_mixer() != 0){
		printf("初始化混音器失败\n");
		fclose(fp);
		snd_pcm_close(pcm_handle);
		free(buff);
		return 0;
	}


	enable_raw_mode(); // 启用非标准模式
	pthread_create(&playback_thread, NULL, playback_thread_func, NULL);
	pthread_create(&volume_thread, NULL, volume_control_thread, NULL);

	pthread_join(playback_thread, NULL); //阻塞等待
	playback_thread = 0; // 播放线程退出
	pthread_cancel(volume_thread); // 取消音量控制线程
	pthread_join(volume_thread, NULL); // 阻塞等待
	volume_thread = 0; // 音量控制线程退出
	disable_raw_mode(); // 恢复标准模式

	printf("\n播放完成\n");

	fclose(fp);
	snd_pcm_close(pcm_handle);
	snd_mixer_close(mixer_handle);
	free(buff);
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

