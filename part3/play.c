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
}

int init_pcm() {
    if (snd_pcm_hw_params_malloc(&hw_params) < 0) {
        printf("分配snd_pcm_hw_params_t结构体失败\n");
        return 1;
    }

    pcm_name = strdup("default");
    if (snd_pcm_open(&pcm_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        printf("打开PCM设备失败\n");
		free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_any(pcm_handle, hw_params) < 0) {
        printf("初始化配置空间失败\n");
        free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_test_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        printf("测试交错模式失败\n");
        free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_set_format(pcm_handle, hw_params, pcm_format) < 0) {
        printf("设置样本长度失败\n");
       	free_pcm_resources();
        return 1;
    }

    unsigned int exact_rate = rate;
    if (snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &exact_rate, 0) < 0) {
        printf("设置采样率失败\n");
        free_pcm_resources();
        return 1;
    }

    if (snd_pcm_hw_params_set_channels(pcm_handle, hw_params, wav_header.num_channels) < 0) {
        printf("设置通道数失败\n");
        free_pcm_resources();
        return 1;
    }

    buffer_size = period_size * periods;
    buff = (unsigned char *)malloc(buffer_size);
    if (!buff) {
        printf("分配缓冲区失败\n");
        free_pcm_resources();
        return 1;
    }

    // 根据采样位数设置缓冲区大小
    if (wav_header.bits_per_sample == 16) {
        frames = buffer_size >> 2;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S16_LE OR S16_BE缓冲区失败\n");
            free_pcm_resources();
            return 1;
        }
    } else if (wav_header.bits_per_sample == 24 && wav_header.block_align == wav_header.num_channels * 3) {
        frames = buffer_size / 6;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S24_3LE OR S24_3BE缓冲区失败\n");
            free_pcm_resources();
            return 1;
        }
    } else if (wav_header.bits_per_sample == 32 || (wav_header.bits_per_sample == 24 && wav_header.block_align == wav_header.num_channels * 4)) {
        frames = buffer_size >> 3;
        if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hw_params, frames) < 0) {
            printf("设置S32_LE OR S32_BE OR S24_LE OR S24_BE缓冲区失败\n");
            free_pcm_resources();
            return 1;
        }
    }

    if (snd_pcm_hw_params(pcm_handle, hw_params) < 0) {
        printf("设置的硬件配置参数失败\n");
        free_pcm_resources();
        return 1;
    }

	free(pcm_name);
    pcm_name = NULL;
    snd_pcm_hw_params_free(hw_params); // 释放 hw_params
    hw_params = NULL;
    return 0;
}



void *playback_thread_func(void *arg) {

    // 初始化播放参数
    pthread_mutex_lock(&mutex);
    played_bytes = 0;
    pthread_mutex_unlock(&mutex);

    while(1){ // 播放线程循环

        // 检查标志
        pthread_mutex_lock(&mutex);
        if (exit_flag) {
            pthread_mutex_unlock(&mutex);
            break; // 退出线程
        }
        while (pause_flag) {
            printf("Thread paused...\n");
            pthread_cond_wait(&cond, &mutex);  // 会释放 mutex 并等待 cond 被 signal
        }
        pthread_mutex_unlock(&mutex);

        // 播放音频
        int read_bytes = fread(buff, 1, buffer_size, fp);

        if(read_bytes == 0){ // 读取到文件末尾
            pthread_mutex_lock(&mutex);
            finish_flag = true;
            pthread_mutex_unlock(&mutex);
            break;
        }
        if(read_bytes < 0){
            printf("\n文件读取错误: %s \n", strerror(errno));
            pthread_mutex_lock(&mutex);
            error_flag = true;
            pthread_mutex_unlock(&mutex);
            break;
        }

        int written = 0;
        while (written < read_bytes) {
            int frames_to_write = (read_bytes - written) / wav_header.block_align;
            int write_ret = snd_pcm_writei(pcm_handle, buff + written, frames_to_write);
            if (write_ret < 0) {
                if (write_ret == -EPIPE) {
                    printf("\nunderrun occurred -32, err_info = %s \n", snd_strerror(write_ret));
                    snd_pcm_prepare(pcm_handle);
                } else {
                    printf("\nret value is : %d \n", write_ret);
                    printf("\nwrite to audio interface failed: %s \n", snd_strerror(write_ret));
                    pthread_mutex_lock(&mutex);
                    error_flag = true;
                    pthread_mutex_unlock(&mutex);
                    break;
                }
            } else if (write_ret > 0) {
                pthread_mutex_lock(&mutex);
                played_bytes += write_ret * wav_header.block_align;
                pthread_mutex_unlock(&mutex);
                written += write_ret * wav_header.block_align;
            }
        }
    }
    return NULL;
}
