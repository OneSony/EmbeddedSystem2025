#include "main.h"
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// 读取目录下所有wav文件名到wav_files数组
int load_wav_files_from_dir(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        LOG_ERROR("无法打开目录: %s", dir_path);
        return -1;
    }
    struct dirent *entry;
    wav_file_count = 0;
    while ((entry = readdir(dir)) != NULL) {
		if (strlen(dir_path) + 1 + strlen(entry->d_name) + 1 > MAX_FILENAME_LEN) {
			LOG_ERROR("文件路径过长: %s/%s", dir_path, entry->d_name);
			return -1;
		}
        int len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".wav") == 0) {
            if (wav_file_count < MAX_WAV_FILES) {
                wav_files[wav_file_count] = malloc(MAX_FILENAME_LEN);
                snprintf(wav_files[wav_file_count], MAX_FILENAME_LEN, "%s/%s", dir_path, entry->d_name);
                wav_file_count++;
            }
        }
    }
    closedir(dir);
    LOG_INFO("从目录 %s 加载了 %d 个WAV文件", dir_path, wav_file_count);
    return 0;
}

// 自定义信号处理函数
void handle_sigint(int sig) {
    LOG_INFO("收到退出信号，正在退出程序");

	if (playback_thread != 0) {
		pthread_mutex_lock(&mutex);
        exit_flag = true;
        pthread_mutex_unlock(&mutex);
        pthread_cond_broadcast(&cond); // 唤醒播放线程，防止其卡在等待
		//pthread_cancel(playback_thread);
		pthread_join(playback_thread, NULL);
		playback_thread = 0;
	}

	if (control_thread != 0) {
		pthread_mutex_lock(&mutex);
        control_end_flag = true;
        pthread_mutex_unlock(&mutex);
		//pthread_cancel(control_thread);
		pthread_join(control_thread, NULL);
		control_thread = 0;
	}

	if (ui_thread != 0) {
		pthread_cancel(ui_thread);
		pthread_join(ui_thread, NULL);
		ui_thread = 0;
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

    // 关闭日志系统
    close_logger();

	printf("\033[H\033[J"); // 清屏并移动光标到左上角

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
    // 初始化日志系统
    if (init_logger("music_player.log") != 0) {
        return -1;
    }
    
    LOG_INFO("音乐播放器程序启动");
    LOG_INFO("命令行参数个数: %d", argc);
    for (int i = 0; i < argc; i++) {
        LOG_INFO("参数[%d]: %s", i, argv[i]);
    }
    
	signal(SIGINT, handle_sigint);

	int ret;
	bool flag = true;

	printf("\033[H\033[J"); // 清屏并移动光标到左上角

	// 分析文件
	while((ret = getopt(argc,argv,"m:")) != -1){
		flag = false;
		switch(ret){
			case 'm': {
				// 如果是目录，读取所有wav文件
				struct stat st;
				if (stat(optarg, &st) == 0 && S_ISDIR(st.st_mode)) {
					LOG_INFO("检测到目录参数: %s", optarg);
					if (load_wav_files_from_dir(optarg) != 0 || wav_file_count == 0) {
						LOG_ERROR("目录下没有wav文件或加载失败");
						close_logger();
						return 0;
					}

				} else {
					// 把这首加到list
					if (wav_file_count < MAX_WAV_FILES) {
						wav_files[wav_file_count] = malloc(MAX_FILENAME_LEN);
						snprintf(wav_files[wav_file_count], MAX_FILENAME_LEN, "%s", optarg);
						wav_file_count++;
						LOG_INFO("添加音频文件: %s", optarg);
					} else {
						LOG_ERROR("wav文件数量超过限制");
						close_logger();
						return 0;
					}
				}
			}
		}
	}

	if (wav_file_count == 0) {
		LOG_ERROR("没有找到任何音频文件");
		close_logger();
		return 0;
	}

	enable_raw_mode(); // 启用非标准模式
	LOG_INFO("启用终端原始模式");
	LOG_INFO("创建控制线程和UI线程");
	
	pthread_create(&control_thread, NULL, control_thread_func, NULL);
	pthread_create(&ui_thread, NULL, ui_thread_func, NULL);

	pthread_join(control_thread, NULL); //阻塞等待
	pthread_cancel(ui_thread);
	pthread_join(ui_thread, NULL); // 等待UI线程结束
	control_thread = 0; // 音量控制线程退出
	ui_thread = 0; // UI线程退出
	disable_raw_mode(); // 恢复标准模式
	LOG_INFO("恢复终端标准模式");

	LOG_INFO("音乐播放器程序正常结束");
	close_logger();

	printf("\033[H\033[J"); // 清屏并移动光标到左上角
	
	signal(SIGINT, SIG_DFL);
	return 0;
}
