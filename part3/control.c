#include "main.h"

int play_track(int track_index) {
    pthread_mutex_lock(&mutex);
    exit_flag = false; // 重置退出标志
    pthread_mutex_unlock(&mutex);
    if (track_index < 0 || track_index >= wav_file_count) {
        printf("无效的曲目索引\n");
        LOG_ERROR("无效的曲目索引: %d", track_index);
        return -1;
    }

    LOG_INFO("开始播放曲目: %s", wav_files[track_index]);

    fp = fopen(wav_files[track_index], "rb");
	if(fp == NULL){
		printf("打开文件失败: %s \n", strerror(errno));
		LOG_ERROR("打开文件失败: %s, 错误: %s", wav_files[track_index], strerror(errno));
		return -1;
	}

	// 只读取到bits_per_sample为止
	size_t header_basic_size = offsetof(struct WAV_HEADER, sub_chunk2_id);
    fread(&wav_header, header_basic_size, 1, fp);

    if (wav_header.audio_format != 1) { // 1表示PCM格式
	    printf("不支持的音频格式: %d\n", wav_header.audio_format);
	    return 0;
	}
    
    // 跳过非data块，找到data块
    char chunk_id[4];
    int chunk_size;
    while (1) {
        if (fread(chunk_id, 1, 4, fp) != 4) {
            printf("未找到data块\n");
            LOG_ERROR("未找到data块");
            return -1;
        }
        if (fread(&chunk_size, 4, 1, fp) != 1) {
            printf("读取chunk大小失败\n");
            LOG_ERROR("读取chunk大小失败");
            return -1;
        }
        if (memcmp(chunk_id, "data", 4) == 0) {
            wav_header.sub_chunk2_size = chunk_size; // 正确设置data块长度
            data_offset_in_file = ftell(fp); // 记录data块的偏移位置
            break;
        } else {
            // 跳过这个chunk的数据
            fseek(fp, chunk_size, SEEK_CUR);
        }
    }


    rate = wav_header.sample_rate;

    bool little_endian;
    if (strncmp(wav_header.chunk_id, "RIFF", 4) == 0) {
        little_endian = true;
    } else if (strncmp(wav_header.chunk_id, "RIFX", 4) == 0) {
        little_endian = false;
    } else {
        printf("未知文件格式\n");
        LOG_ERROR("未知文件格式");
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
                LOG_ERROR("不支持的采样位数: %d", wav_header.bits_per_sample);
                return -1;
            }
        }else{
            if(wav_header.block_align == wav_header.num_channels * 4){
                pcm_format = SND_PCM_FORMAT_S24_BE;
            }else if(wav_header.block_align == wav_header.num_channels * 3){
                pcm_format = SND_PCM_FORMAT_S24_3BE;
            }else{
                printf("不支持的采样位数: %d\n", wav_header.bits_per_sample);
                LOG_ERROR("不支持的采样位数: %d", wav_header.bits_per_sample);
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
        LOG_ERROR("不支持的采样位数: %d", wav_header.bits_per_sample);
        return -1;
    }

    ws_cfg.frame_size   = (int)(wav_header.sample_rate * 0.03f);
    ws_cfg.overlap_size = ws_cfg.frame_size / 2;
    ws_cfg.speed_ratio  = playback_speed;  // 初始播放速率
    if (wsola_state_init(&ws_state,
                         &ws_cfg,
                         wav_header.num_channels,
                         wav_header.bits_per_sample,
                         periods * period_size / (wav_header.num_channels * wav_header.bits_per_sample / 8)) != 0) {
        fprintf(stderr, "WSOLA 初始化失败\n");
        return 1;
    }

    // 初始化
	if(init_pcm() != 0){
		printf("初始化PCM失败\n"); // PCM内部的资源已经释放
		LOG_ERROR("初始化PCM失败");
		if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
		return -1;
	}

	if(init_mixer() != 0){
		printf("初始化混音器失败\n");
		LOG_ERROR("初始化混音器失败");
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
    LOG_INFO("成功开始播放曲目: %s", wav_files[track_index]);
  
    return 0;
}

void end_playback() {

    if(playback_thread != 0) {
        pthread_mutex_lock(&mutex);
        exit_flag = true;
        pthread_mutex_unlock(&mutex);
        pthread_cond_broadcast(&cond); // 唤醒播放线程，防止其卡在等待
        pthread_cancel(playback_thread);
        pthread_join(playback_thread, NULL); // 等待播放线程安全退出
        playback_thread = 0; // 重置播放线程
    }

    free_pcm_resources();
    free_mixer_resources();
    if (fp != NULL) {
        fclose(fp);
        fp = NULL;
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
        LOG_ERROR("无法打开混音器");
        return 1;
    }

    if (snd_mixer_attach(mixer_handle, "default") < 0) {
        printf("无法附加混音器\n");
        LOG_ERROR("无法附加混音器");
        free_mixer_resources();
        return 1;
    }

    if (snd_mixer_selem_register(mixer_handle, NULL, NULL) < 0) {
        printf("无法注册混音器控件\n");
        LOG_ERROR("无法注册混音器控件");
        free_mixer_resources();
        return 1;
    }

    if (snd_mixer_load(mixer_handle) < 0) {
        printf("无法加载混音器控件\n");
        LOG_ERROR("无法加载混音器控件");
        free_mixer_resources();
        return 1;
    }

    if (snd_mixer_selem_id_malloc(&sid) < 0) {
        printf("无法分配控件 ID\n");
        LOG_ERROR("无法分配控件 ID");
        free_mixer_resources();
        return 1;
    }

    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, "Master");

    elem = snd_mixer_find_selem(mixer_handle, sid);
    if (!elem) {
        printf("无法找到 Master 控件\n");
        LOG_ERROR("无法找到 Master 控件");
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
    LOG_INFO("混音器初始化成功");
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
            end_playback();
            LOG_INFO("当前曲目播放完毕");

            finish_flag = false; // 重置播放完成标志
            // 切换到下一曲目
            exit_flag = false;
            pause_flag = false;
            track_index = (track_index + 1) % wav_file_count;
            play_track(track_index);
            //printf("当前曲目: %s\n", wav_files[track_index]);
            LOG_INFO("自动切换到下一曲目: %s", wav_files[track_index]);
        }


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

                pthread_mutex_lock(&mutex);
                if (input == 'D') { // 左方向键
                    if (current_volume > min_volume) {
                        current_volume -= 5; // 减小音量
                        LOG_USER("用户操作: 音量减小到 %ld%%", current_volume);
                    } else {
                        LOG_USER("用户操作: 尝试减小音量，但已达到最小音量");
                    }
                } else if (input == 'C') { // 右方向键
                    if (current_volume < max_volume) {
                        current_volume += 5; // 增大音量
                        LOG_USER("用户操作: 音量增加到 %ld%%", current_volume);
                    } else {
                        LOG_USER("用户操作: 尝试增加音量，但已达到最大音量");
                    }
                }

                if (current_volume < min_volume) current_volume = min_volume;
                if (current_volume > max_volume) current_volume = max_volume;
                pthread_mutex_unlock(&mutex);

                pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
                long min, max;
                snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
                long set_volume = min + (current_volume * (max - min) / 100);
                if (snd_mixer_selem_set_playback_volume_all(elem, set_volume) == 0) {
                    LOG_INFO("音量设置成功: %ld%%", current_volume);
                } else {
                    LOG_ERROR("音量设置失败");
                }
                pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            } else if (input == 'q') { // 'q' 键退出
                LOG_USER("用户操作: 退出播放器");
                end_playback(); // 结束当前播放

                break; // 退出循环
            } else if (input == 'p') { // 'p' 键暂停
                pthread_mutex_lock(&mutex);
                pause_flag = !pause_flag;
                pthread_mutex_unlock(&mutex);
                //printf("\n%s\n", pause_flag ? "已暂停" : "已恢复");
                LOG_USER("用户操作: %s", pause_flag ? "暂停播放" : "恢复播放");
                if(playback_thread != 0) {
                    if (pause_flag) {
                        // 先暂停声卡
                        if (snd_pcm_pause(pcm_handle, 1) < 0) {
                            printf("\n声卡不支持无缝暂停\n");
                            LOG_WARNING("声卡不支持无缝暂停");
                        } else {
                            LOG_INFO("暂停成功");
                        }
                    } else {
                        // 先恢复声卡
                        if (snd_pcm_pause(pcm_handle, 0) < 0) {
                            printf("\n声卡不支持无缝暂停\n");
                            LOG_WARNING("声卡不支持无缝恢复");
                        } else {
                            LOG_INFO("恢复播放成功");
                        }
                        pthread_cond_signal(&cond); // 唤醒播放线程
                    }
                }
            } else if (input == 'n') { // 'n' 键切换到下一曲目
                //printf("\n切换到下一曲目...\n");
                LOG_USER("用户操作: 切换到下一曲目");
                end_playback(); // 结束当前播放

                // 切换到下一曲目
                exit_flag = false;
                pause_flag = false;
                track_index = (track_index + 1) % wav_file_count;
                play_track(track_index);
                //printf("\n当前曲目: %s\n", wav_files[track_index]);
                LOG_INFO("切换到下一曲目: %s", wav_files[track_index]);

            } else if (input == 'b') { // 'b' 键切换到上一曲目
                LOG_USER("用户操作: 切换到上一曲目");
                end_playback(); // 结束当前播放

                // 切换到上一曲目
                exit_flag = false;
                pause_flag = false;
                track_index = (track_index - 1 + wav_file_count) % wav_file_count;
                play_track(track_index);
                //printf("\n当前曲目: %s\n", wav_files[track_index]);
                LOG_INFO("切换到上一曲目: %s", wav_files[track_index]);
            } else if (input == 'f') { // 快进10秒
                LOG_USER("用户操作: 快进10秒");
                pthread_mutex_lock(&mutex);
                long forward_bytes = 10 * wav_header.byte_rate;
                long target = played_bytes + forward_bytes;
                if (target > wav_header.sub_chunk2_size)
                    target = wav_header.sub_chunk2_size;
                played_bytes = target;
                fseek(fp, data_offset_in_file + played_bytes, SEEK_SET); // 跳转文件指针
                pthread_mutex_unlock(&mutex);
                LOG_INFO("快进操作完成");
            } else if (input == 'r') { // 快退10秒
                LOG_USER("用户操作: 快退10秒");
                pthread_mutex_lock(&mutex);
                long rewind_bytes = 10 * wav_header.byte_rate;
                long target = played_bytes - rewind_bytes;
                if (target < 0)
                    target = 0;
                played_bytes = target;
                fseek(fp, data_offset_in_file + played_bytes, SEEK_SET); // 跳转文件指针
                pthread_mutex_unlock(&mutex);
                LOG_INFO("快退操作完成");
             } else if (input == 's') { // 's' 键切换倍速
                pthread_mutex_lock(&mutex);
                if (playback_speed == 0.5) {
                    playback_speed = 1.0;
                } else if (playback_speed == 1.0) {
                    playback_speed = 1.5;
                } else if (playback_speed == 1.5) {
                    playback_speed = 2.0;
                } else {
                    playback_speed = 0.5;
                }
                //printf("\n当前播放速度: %fx\n", playback_speed);
                pthread_mutex_unlock(&mutex);
            } else if (input == 'e') { // 'e' 键切换均衡器开关
                equalizer.enabled = !equalizer.enabled;
                //printf("\n均衡器: %s\n", equalizer.enabled ? "开启" : "关闭");
            } else if (input >= '0' && input <= '3') { // 数字键0-3切换均衡器预设
                eq_preset_t preset = (eq_preset_t)(input - '0');
                if (preset < EQ_NUM_PRESETS) {
                    equalizer_set_preset(&equalizer, preset);
                    //printf("\n切换到均衡器预设: %d\n", preset);
                }
            }
        } else {
            continue; // 没有输入则继续循环
        }
    }

    return NULL;
}
