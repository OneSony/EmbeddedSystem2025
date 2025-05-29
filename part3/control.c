#include "main.h"


void free_mixer_resources() {
    // 释放 sid
    if (sid != NULL) {
        snd_mixer_selem_id_free(sid);
        sid = NULL;
    }

    // 关闭 mixer_handle
    if (mixer_handle != NULL) {
        snd_mixer_close(mixer_handle);
        mixer_handle = NULL;
    }
}


int init_mixer() {
    if (snd_mixer_open(&mixer_handle, 0) < 0) {
        printf("无法打开混音器\n");
        return 1;
    }

    if (snd_mixer_attach(mixer_handle, "default") < 0) {
        printf("无法附加混音器\n");
        free_mixer_resources();
        return 1;
    }

    if (snd_mixer_selem_register(mixer_handle, NULL, NULL) < 0) {
        printf("无法注册混音器控件\n");
        free_mixer_resources();
        return 1;
    }

    if (snd_mixer_load(mixer_handle) < 0) {
        printf("无法加载混音器控件\n");
        free_mixer_resources();
        return 1;
    }

    if (snd_mixer_selem_id_malloc(&sid) < 0) {
        printf("无法分配控件 ID\n");
        free_mixer_resources();
        return 1;
    }

    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

    elem = snd_mixer_find_selem(mixer_handle, sid);
    if (!elem) {
        printf("无法找到 Master 控件\n");
        free_mixer_resources();
        return 1;
    }

    // 设置初始音量
    long min, max;
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    long set_volume = min + (init_volume * (max - min) / 100);
    snd_mixer_selem_set_playback_volume_all(elem, set_volume);

    snd_mixer_selem_id_free(sid); // 释放 sid
	sid = NULL;
    return 0;
}

void *control_thread_func(void *arg) {

    //TODO 控制文件播放
    //维护一个播放列表
    //检查play线程是否播放完毕??

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
        } else if (input == 'q') { // 'q' 键退出
            pthread_mutex_lock(&mutex);
            exit_flag = true;
            pthread_mutex_unlock(&mutex);
            break; // 退出循环
        } else if (input == 'p') { // 'p' 键暂停
            pthread_mutex_lock(&mutex);
            pause_flag = !pause_flag;
            if (pause_flag) {
                // 先暂停声卡
                if (snd_pcm_pause(pcm_handle, 1) < 0) {
                    printf("\n声卡不支持无缝暂停\n");
                }
            } else {
                // 先恢复声卡
                if (snd_pcm_pause(pcm_handle, 0) < 0) {
                    printf("\n声卡不支持无缝暂停\n");
                }
                pthread_cond_signal(&cond); // 唤醒播放线程
            }
            pthread_mutex_unlock(&mutex);
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
