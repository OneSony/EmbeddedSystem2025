#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include <termios.h>
#include "const.h"

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

int init_mixer(){

	snd_mixer_open(&mixer_handle, 0);
    snd_mixer_attach(mixer_handle, "default");
    snd_mixer_selem_register(mixer_handle, NULL, NULL);
    snd_mixer_load(mixer_handle);

	// 设置简单控件（Simple Element）的 ID
    snd_mixer_selem_id_malloc(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

	elem = snd_mixer_find_selem(mixer_handle, sid);

    if (!elem) {
        //printf("无法找到 Master 控件\n");
        snd_mixer_close(mixer_handle);
        return 1;
    }

	// 设置初始音量
	long min, max;
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	long set_volume = min + (init_volume * (max - min) / 100);
	snd_mixer_selem_set_playback_volume_all(elem, set_volume);

	return 0;
}

void *volume_control_thread(void *arg) {

	enable_raw_mode(); // 启用非标准模式

    int volume = init_volume; // 初始音量值
    const int max_volume = 100;
    const int min_volume = 0;

	printf("使用左键减少音量，右键增加音量\n");

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
        printf("] %d%%", volume);
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

        long min, max;
		snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
		long set_volume = min + (volume * (max - min) / 100);
		snd_mixer_selem_set_playback_volume_all(elem, set_volume);
    }

	disable_raw_mode(); // 恢复标准模式
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
			
			printf("end of music file input! \n");
			exit(1);
		}
		
		if(ret < 0){
			
			printf("read pcm from file! \n");
			exit(1);
		}

		// 向PCM设备写入数据,
		while((ret = snd_pcm_writei(pcm_handle, buff, frames)) < 0){
			if (ret == -EPIPE){
				
                  /* EPIPE means underrun -32  的错误就是缓存中的数据不够 */
                  printf("underrun occurred -32, err_info = %s \n", snd_strerror(ret));
		 //完成硬件参数设置，使设备准备好
                  snd_pcm_prepare(pcm_handle);
			
            } else if(ret < 0){
				printf("ret value is : %d \n", ret);
				debug_msg(-1, "write to audio interface failed");
			}
		}
	}

	fprintf(stderr, "\nend of music file input\n");

}


int main(int argc, char *argv [])
{
	atexit(disable_raw_mode); // 程序退出时恢复终端设置

	int ret, rate_arg, format_arg;
	bool flag = true;

	while((ret = getopt(argc,argv,"m:f:r:")) != -1){
		flag = false;
		switch(ret){
			case 'm':
				printf("打开文件 \n");
				open_music_file(optarg);
				break;
			case 'f':
				
				format_arg = atoi(optarg);
				
				// 判断是哪种采样位
				switch(format_arg){
					case 161:
						printf("format_arg value is : S16LE \n");
						pcm_format = SND_PCM_FORMAT_S16_LE;
						break;
					
					case 162:
						printf("format_arg value is : S16BE \n");
						pcm_format = SND_PCM_FORMAT_S16_BE;
						break;
					
					case 201:
						printf("format_arg value is : S20LE \n");
						//pcm_format = SND_PCM_FORMAT_S20_LE;
						break;
					
					case 202:
						printf("format_arg value is : S20BE \n");
						//pcm_format = SND_PCM_FORMAT_S20_BE;
						break;

					case 241:
						printf("format_arg value is : S24LE \n");
						pcm_format = SND_PCM_FORMAT_S24_LE;
						break;

					case 242:
						printf("format_arg value is : S24BE \n");
						pcm_format = SND_PCM_FORMAT_S24_BE;
						break;

					case 2431:
						printf("format_arg value is : S243LE \n");
						pcm_format = SND_PCM_FORMAT_S24_3LE;
						break;
					
					case 2432:
						printf("format_arg value is : S243BE \n");
						pcm_format = SND_PCM_FORMAT_S24_3BE;
						break;

					case 321:
						printf("format_arg value is : S32LE \n");
						pcm_format = SND_PCM_FORMAT_S32_LE;
						break;

					case 322:
						printf("format_arg value is : S32BE \n");
						pcm_format = SND_PCM_FORMAT_S32_BE;
						break;
						

				}
				break;
				
			case 'r':
				
				rate_arg = atoi(optarg);
				
				if(rate_arg == 44){
					
					printf("rate_arg value is : 44.1HZ \n");
					rate = 44100;
				
				}else if(rate_arg == 88){
					
					printf("rate_arg value is : 88.2HZ \n");
					rate = 88200;
					
				}else{
					
					printf("rate_arg value is : 8HZ \n");
					rate = 8000;
					
				}
				break;
		}
	}

	if(flag){
		printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
		printf("Either 1'st, 2'nd, 3'th or all parameters were missing \n");
		printf("\n");
		printf("1'st : -m [music_filename] \n");
		printf("		music_filename.wav \n");
		printf("\n");
		printf("2'nd : -f [format 241bit or 16bit or 32bit] \n");
		printf("		161 for S16_LE, 162 for S16_BE \n");
		printf("		241 for S24_LE, 242 for S24_BE \n");
		printf("		2431 for S24_3LE, 2432 for S24_3BE \n");
		printf("		321 for S32_LE, 322 for S32_BE \n");
		printf("\n");
		printf("3'th : -r [rate,44 or 88] \n");
		printf("		44 for 44100hz \n");
		printf("		82 for 88200hz \n");
		printf("\n");
		printf("For example: ./alsa -m 1.wav -f 161 -r 44 \n");
		printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
		exit(1);
	}
	

	
	// 在堆栈上分配snd_pcm_hw_params_t结构体的空间，参数是配置pcm硬件的指针,返回0成功
	debug_msg(snd_pcm_hw_params_malloc(&hw_params), "分配snd_pcm_hw_params_t结构体");
	

	// 打开PCM设备 返回0 则成功，其他失败
	// 函数的最后一个参数是配置模式，如果设置为0,则使用标准模式
	// 其他值位SND_PCM_NONBLOCL和SND_PCM_ASYNC 如果使用NONBLOCL 则读/写访问, 如果是后一个就会发出SIGIO
	pcm_name = strdup("default");
	debug_msg(snd_pcm_open(&pcm_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0), "打开PCM设备");

	// 在我们将PCM数据写入声卡之前，我们必须指定访问类型，样本长度，采样率，通道数，周期数和周期大小。
	// 首先，我们使用声卡的完整配置空间之前初始化hwparams结构
	debug_msg(snd_pcm_hw_params_any(pcm_handle, hw_params), "配置空间初始化");

	// 设置交错模式（访问模式）
	// 常用的有 SND_PCM_ACCESS_RW_INTERLEAVED（交错模式） 和 SND_PCM_ACCESS_RW_NONINTERLEAVED （非交错模式） 
	// 参考：https://blog.51cto.com/yiluohuanghun/868048
	// 一般为交错模式
	debug_msg(snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED), "设置交错模式（访问模式）");

	//TODO 为什么要手动设置format
	debug_msg(snd_pcm_hw_params_set_format(pcm_handle, hw_params, pcm_format), "设置样本长度(位数)");
	
	//TODO 为什么要手动设置rate
	unsigned int exact_rate = rate; // ALSA 可能会调整采样率
	debug_msg(snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &exact_rate, 0), "设置采样率");
	if (exact_rate != rate) {
		printf("警告: 采样率被调整为 %u\n", exact_rate);
	}

	debug_msg(snd_pcm_hw_params_set_channels(pcm_handle, hw_params, wav_header.num_channels), "设置通道数");

	// 设置缓冲区 buffer_size = period_size * periods 一个缓冲区的大小可以这么算，我上面设定了周期为2，
	// 周期大小我们预先自己设定，那么要设置的缓存大小就是 周期大小 * 周期数 就是缓冲区大小了。
	buffer_size = period_size * periods;
	
	// 为buff分配buffer_size大小的内存空间
	buff = (unsigned char *)malloc(buffer_size); // 用户区的buff
	
	if(format_arg == 161 || format_arg == 162){

		frames = buffer_size >> 2;
	
		debug_msg(snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames), "设置S16_LE OR S16_BE缓冲区");
		
	}else if(format_arg == 2431 || format_arg == 2432){

		frames = buffer_size / 6;
		
		/*
			当位数为24时，就需要除以6了，因为是24bit * 2 / 8 = 6
		*/
		debug_msg(snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames), "设置S24_3LE OR S24_3BE的缓冲区");
		
	}else if(format_arg == 321 || format_arg == 322 || format_arg == 241 || format_arg == 242){

		frames = buffer_size >> 3;
		/*
			当位数为32时，就需要除以8了，因为是32bit * 2 / 8 = 8
		*/
		debug_msg(snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames), "设置S32_LE OR S32_BE OR S24_LE OR S24_BE缓冲区");

	}


	// 设置的硬件配置参数，加载，并且会自动调用snd_pcm_prepare()将stream状态置为SND_PCM_STATE_PREPARED
	debug_msg(snd_pcm_hw_params(pcm_handle, hw_params), "设置的硬件配置参数");
	snd_pcm_hw_params_free(hw_params);

	if(init_mixer() != 0){
		printf("初始化混音器失败\n");
		exit(1);
	}

	pthread_create(&playback_thread, NULL, playback_thread_func, NULL);
	pthread_create(&volume_thread, NULL, volume_control_thread, NULL);

	pthread_join(playback_thread, NULL); //阻塞等待
	pthread_cancel(volume_thread); // 取消音量控制线程
	pthread_join(volume_thread, NULL); // 阻塞等待

	// 关闭文件
	fclose(fp);
	snd_pcm_close(pcm_handle);
	snd_mixer_selem_id_free(sid);
	free(pcm_name);
	free(buff);
	return 0;

}

void open_music_file(const char *path_name){

	// TODO: 打开音频文件，输出其文件头信息
	// 直接复制part1的相关代码到此处即可

	fp = fopen(path_name, "rb");
	if(fp == NULL){
		printf("打开文件失败, err_info = %s \n", strerror(errno));
		exit(1);
	}

	// 读取文件头
	fread(&wav_header, sizeof(struct WAV_HEADER), 1, fp);

	// 打印文件头信息
	printf("wav文件头结构体大小: %d\n", (int)sizeof(wav_header));
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
    printf("数据块长度: %d\n", wav_header.sub_chunk2_size);

}


bool debug_msg(int result, const char *str)
{
	if(result < 0){
		printf("err: %s 失败!, result = %d, err_info = %s \n", str, result, snd_strerror(result));
		exit(1);
	}
	return true;
}

