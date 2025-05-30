#include "main.h"

int play_track(int track_index) {
    if (track_index < 0 || track_index >= wav_file_count) {
        printf("无效的曲目索引\n");
        return -1;
    }

    fp = fopen(wav_files[track_index], "rb");
	if(fp == NULL){
		printf("打开文件失败: %s \n", strerror(errno));
		return -1;
	}

	// 读取文件头
	fread(&wav_header, sizeof(struct WAV_HEADER), 1, fp);

    rate = wav_header.sample_rate;

    bool little_endian;
    if (strncmp(wav_header.chunk_id, "RIFF", 4) == 0) {
        little_endian = true;
    } else if (strncmp(wav_header.chunk_id, "RIFX", 4) == 0) {
        little_endian = false;
    } else {
        printf("未知文件格式\n");
        return -1;
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
                return -1;
            }
        }else{
            if(wav_header.block_align == wav_header.num_channels * 4){
                pcm_format = SND_PCM_FORMAT_S24_BE;
            }else if(wav_header.block_align == wav_header.num_channels * 3){
                pcm_format = SND_PCM_FORMAT_S24_3BE;
            }else{
                printf("不支持的采样位数: %d\n", wav_header.bits_per_sample);
                return -1;
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
        return -1;
    }

    // 初始化
	if(init_pcm() != 0){
		printf("初始化PCM失败\n"); // PCM内部的资源已经释放
		if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
		return -1;
	}

	if(init_mixer() != 0){
		printf("初始化混音器失败\n");
        free_pcm_resources();
        if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
		return -1;
	}

    //设置音量
    long volume = current_volume; // 使用局部变量, 只有键盘控制的部分直接修改全局变量
    const int max_volume = 100;
    const int min_volume = 0;

    if (volume < min_volume) volume = min_volume;
    if (volume > max_volume) volume = max_volume;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
    long min, max;
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    long set_volume = min + (volume * (max - min) / 100);
    snd_mixer_selem_set_playback_volume_all(elem, set_volume);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);


    pthread_create(&playback_thread, NULL, playback_thread_func, NULL);

}

void end_playback() {

    if(playback_thread == 0) {

    }else{
        pthread_mutex_lock(&mutex);
        exit_flag = true;
        pthread_mutex_unlock(&mutex);
        pthread_join(playback_thread, NULL); // 等待播放线程安全退出
        playback_thread = 0; // 重置播放线程
        free_pcm_resources();
        free_mixer_resources();
        if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
    }
}

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

    const int max_volume = 100;
    const int min_volume = 0;
	

    play_track(track_index); // 播放当前曲目
    //printf("当前曲目: %s\n", wav_files[track_index]);


    while (1) {

        if(finish_flag) {
            //printf("\n当前曲目播放完毕\n");
            pthread_join(playback_thread, NULL); // 等待播放线程安全退出
            playback_thread = 0; // 重置播放线程
            finish_flag = false; // 重置播放完成标志
            // 切换到下一曲目
            track_index = (track_index + 1) % wav_file_count;
            play_track(track_index);
            //printf("当前曲目: %s\n", wav_files[track_index]);
        }


        // 显示音量进度条
        /*printf("\r音量: [");
        for (int i = 0; i < max_volume / 2; i++) {
            if (i < current_volume / 2) {
                printf("#"); // 填充的部分
            } else {
                printf(" "); // 未填充的部分
            }
        }
        printf("] %3ld%%", current_volume);
		fflush(stdout);*/

        // 捕获用户输入
        char input = 0;
        struct timeval tv = {0, 100000}; // 100ms
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(0, &readfds); // 标准输入

        int retval = select(1, &readfds, NULL, NULL, &tv);
        if (retval > 0 && FD_ISSET(0, &readfds)) {
            input = getchar();
            if (input == '\033') { // 检测到转义序列
                getchar();         // 跳过 '['
                input = getchar(); // 获取方向键
                if (input == 'D') { // 左方向键
                    if (current_volume > min_volume) {
                        current_volume -= 5; // 减小音量
                    }
                } else if (input == 'C') { // 右方向键
                    if (current_volume < max_volume) {
                        current_volume += 5; // 增大音量
                    }
                }

                if (current_volume < min_volume) current_volume = min_volume;
                if (current_volume > max_volume) current_volume = max_volume;

                pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
                long min, max;
                snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
                long set_volume = min + (current_volume * (max - min) / 100);
                snd_mixer_selem_set_playback_volume_all(elem, set_volume);
                pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            } else if (input == 'q') { // 'q' 键退出

                end_playback(); // 结束当前播放

                break; // 退出循环
            } else if (input == 'p') { // 'p' 键暂停
                pthread_mutex_lock(&mutex);
                pause_flag = !pause_flag;
                //printf("\n%s\n", pause_flag ? "已暂停" : "已恢复");
                if(playback_thread != 0) {
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
                }
                pthread_mutex_unlock(&mutex);
            } else if (input == 'n') { // 'n' 键切换到下一曲目
                //printf("\n切换到下一曲目...\n");

                end_playback(); // 结束当前播放

                // 切换到下一曲目
                track_index = (track_index + 1) % wav_file_count;
                exit_flag = false;
                pause_flag = false;
                play_track(track_index);
                //printf("\n当前曲目: %s\n", wav_files[track_index]);
                

            } else if (input == 'b') { // 'b' 键切换到上一曲目
                end_playback(); // 结束当前播放

                // 切换到上一曲目
                track_index = (track_index - 1 + wav_file_count) % wav_file_count;
                exit_flag = false;
                pause_flag = false;
                play_track(track_index);
                //printf("\n当前曲目: %s\n", wav_files[track_index]);
            }
        } else {
            continue; // 没有输入则继续循环
        }
        

    }

    return NULL;
}
