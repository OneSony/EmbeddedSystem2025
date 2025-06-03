#include "main.h"


void free_pcm_resources() {
    // 释放 pcm_name
    if (pcm_name != NULL) {
        free(pcm_name);
        pcm_name = NULL;
    }

    // 释放 hw_params
    if (hw_params != NULL) {
        snd_pcm_hw_params_free(hw_params);
        hw_params = NULL;
    }

    // 释放 buff
    if (buff != NULL) {
        free(buff);
        buff = NULL;
    }

    // 如果 pcm_handle 不为空，关闭 PCM 设备
    if (pcm_handle != NULL) {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
    }

    wsola_state_free(&ws_state);
  
    LOG_INFO("PCM资源释放完成");

}

int init_pcm() {
    if (snd_pcm_hw_params_malloc(&hw_params) < 0) {
        printf("分配snd_pcm_hw_params_t结构体失败\n");
        LOG_ERROR("分配snd_pcm_hw_params_t结构体失败");
        return 1;
    }

    pcm_name = strdup("default");
    if (snd_pcm_open(&pcm_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        printf("打开PCM设备失败\n");
        LOG_ERROR("打开PCM设备失败");
		free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_any(pcm_handle, hw_params) < 0) {
        printf("初始化配置空间失败\n");
        LOG_ERROR("初始化配置空间失败");
        free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_test_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        printf("测试交错模式失败\n");
        LOG_ERROR("测试交错模式失败");
        free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_set_format(pcm_handle, hw_params, pcm_format) < 0) {
        printf("设置样本长度失败\n");
        LOG_ERROR("设置样本长度失败");
       	free_pcm_resources();
        return 1;
    }

    unsigned int exact_rate = rate;
    if (snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &exact_rate, 0) < 0) {
        printf("设置采样率失败\n");
        LOG_ERROR("设置采样率失败");
        free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_set_channels(pcm_handle, hw_params, wav_header.num_channels) < 0) {
        printf("设置通道数失败\n");
        LOG_ERROR("设置通道数失败");
        free_pcm_resources();
        return 1;
    }

    buffer_size = period_size * periods;
    buff = (unsigned char *)malloc(buffer_size * MAX_SPEED);
    if (!buff) {
        printf("分配缓冲区失败\n");
        LOG_ERROR("分配缓冲区失败");
        free_pcm_resources();
        return 1;
    }

    // 根据采样位数设置缓冲区大小
    if (wav_header.bits_per_sample == 16) {
        frames = buffer_size >> 2;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S16_LE OR S16_BE缓冲区失败\n");
            LOG_ERROR("设置S16_LE OR S16_BE缓冲区失败");
            free_pcm_resources();
            return 1;
        }
    } else if (wav_header.bits_per_sample == 24 && wav_header.block_align == wav_header.num_channels * 3) {
        frames = buffer_size / 6;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S24_3LE OR S24_3BE缓冲区失败\n");
            LOG_ERROR("设置S24_3LE OR S24_3BE缓冲区失败");
            free_pcm_resources();
            return 1;
        }
    } else if (wav_header.bits_per_sample == 32 || (wav_header.bits_per_sample == 24 && wav_header.block_align == wav_header.num_channels * 4)) {
        frames = buffer_size >> 3;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S32_LE OR S32_BE OR S24_LE OR S24_BE缓冲区失败\n");
            LOG_ERROR("设置S32_LE OR S32_BE OR S24_LE OR S24_BE缓冲区失败");
            free_pcm_resources();
            return 1;
        }
    }

    if (snd_pcm_hw_params(pcm_handle, hw_params) < 0) {
        printf("设置的硬件配置参数失败\n");
        LOG_ERROR("设置的硬件配置参数失败");
        free_pcm_resources();
        return 1;
    }

    // 初始化均衡器
    equalizer_init(&equalizer, wav_header.sample_rate);
    
	free(pcm_name);
    pcm_name = NULL;
    snd_pcm_hw_params_free(hw_params); // 释放 hw_params
    hw_params = NULL;
    LOG_INFO("PCM初始化成功");
    return 0;
}



void *playback_thread_func(void *arg) {

    int bytes_per_frame = wav_header.num_channels * wav_header.bits_per_sample / 8;
    int frame_per_buffer = buffer_size / bytes_per_frame;


    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
    fseek(fp, sizeof(struct WAV_HEADER), SEEK_SET); // 跳过WAV头
    // 清空缓冲区，避免开头杂音
    memset(buff, 0, buffer_size * MAX_SPEED);
    pthread_mutex_lock(&mutex);
    played_bytes = 0;
    pthread_mutex_unlock(&mutex);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断
  
    LOG_INFO("播放线程开始");


    while (1) { // 播放线程循环

        // 检查标志
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
        pthread_mutex_lock(&mutex);
        while (pause_flag && !exit_flag) {
            LOG_INFO("播放线程暂停");
            pthread_cond_wait(&cond, &mutex);
        }
        if (exit_flag) {
            pthread_mutex_unlock(&mutex);
            // 清空缓冲区，避免结尾杂音
            memset(buff, 0, buffer_size * MAX_SPEED);
            snd_pcm_drain(pcm_handle);
            LOG_INFO("收到退出标志，播放线程退出");
            break;
        }
        ws_state.config.speed_ratio = playback_speed;
        float local_speed = playback_speed;
        pthread_mutex_unlock(&mutex);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断

        // 动态调整读取长度：倍速时多读
        int read_size = (int)(buffer_size * local_speed);
        if (read_size > buffer_size * MAX_SPEED) read_size = buffer_size * MAX_SPEED; // 防止越界

        int read_bytes = fread(buff, 1, read_size, fp);

        // 只写入完整帧，丢弃不足一帧的数据
        int in_frames = read_bytes / bytes_per_frame;

        if (in_frames <= 0) {
            // 切下一首
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
            pthread_mutex_lock(&mutex);
            finish_flag = true;
            pthread_mutex_unlock(&mutex);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断
            LOG_INFO("文件播放完毕");
            break;
        }

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
        int out_frames = wsola_state_process(&ws_state, buff, in_frames, buff, frame_per_buffer * 2);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断


        if (out_frames > frame_per_buffer * 2) {
            out_frames = frame_per_buffer; // 截断输出帧数
        }

        // 对音频数据应用均衡器处理（仅支持16位音频）
        if (wav_header.bits_per_sample == 16 && equalizer.enabled) {
            int num_samples = read_bytes / wav_header.block_align;
            equalizer_process_audio(&equalizer, (int16_t *)buff, num_samples, wav_header.num_channels);
        }

        int written = 0;
        while (written < out_frames) {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
            int ret = snd_pcm_writei(pcm_handle, buff + written * bytes_per_frame, out_frames - written);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断

            if (ret < 0) {
                if (ret == -EPIPE) {
                    //printf("\nunderrun occurred -32, err_info = %s \n", snd_strerror(ret));
                    LOG_WARNING("发生underrun: %s", snd_strerror(ret));
                    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
                    snd_pcm_prepare(pcm_handle);
                    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断

                } else {
                    //printf("\nret value is : %d \n", ret);
                    //printf("\nwrite to audio interface failed: %s \n", snd_strerror(ret));
                    LOG_ERROR("写入音频接口失败: 返回值=%d, 错误=%s", ret, snd_strerror(ret));

                    //TODO
                    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
                    pthread_mutex_lock(&mutex);
                    error_flag = true;
                    pthread_mutex_unlock(&mutex);
                    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断
                    break;
                }
            } else {
                written += ret;
            }
        }

        // 更新已播放字节数
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
        pthread_mutex_lock(&mutex);
        played_bytes += read_bytes;
        pthread_mutex_unlock(&mutex);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 恢复中断

    }
    LOG_INFO("播放线程结束");
    return NULL;
}
