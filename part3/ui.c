#include "main.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

// 获取均衡器预设名称
const char* get_preset_name(eq_preset_t preset) {
    switch (preset) {
        case EQ_PRESET_FLAT: return "Flat";
        case EQ_PRESET_BASS_BOOST: return "Bass Boost";
        case EQ_PRESET_TREBLE_BOOST: return "Treble Boost";
        case EQ_PRESET_VOICE_BOOST: return "Voice Boost";
        default: return "Unknown";
    }
}

// 绘制UI函数

void draw_ui(int cur_sec, int total_sec, int volume, int track_index, int wav_file_count, char **wav_files, int pause_flag, float playback_speed) {
    printf("\033[H"); // 只移动光标到左上角，不清屏

    // 曲目列表
    printf("Tracklist:\n");
    for (int i = 0; i < wav_file_count; ++i) {
        if (i == track_index) printf(" > %s\n", wav_files[i]);
        else printf("   %s\n", wav_files[i]);
    }

    // 播放信息 - 简化显示
    printf("\nProgress: %d/%d sec          \n", cur_sec, total_sec);
    printf("Volume:   %d%%\n", volume);
    printf("Status:   [%s]\n", pause_flag ? "Paused " : "Playing");
    printf("Speed:    [%.1fx]\n", playback_speed);
    printf("EQ:       [%s] %s              \n", 
           equalizer.enabled ? "ON " : "OFF", 
           get_preset_name(equalizer.current_preset));
    
    printf("\n");
    // 精简控制说明 - 保持所有功能
    printf("Volume: [+/-]  Track: [n/b]  Seek: [f/r]  Pause: [p]  Speed: [s]\n");
    printf("EQ: [e] Toggle [0] Flat [1] Bass [2] Treble [3] Voice  Quit: [q]\n");
    fflush(stdout);
}

bool is_stdout_ready() {
    fd_set write_fds;
    struct timeval timeout = {0, 0}; // 非阻塞检查
    
    FD_ZERO(&write_fds);
    FD_SET(STDOUT_FILENO, &write_fds);
    
    int result = select(STDOUT_FILENO + 1, NULL, &write_fds, NULL, &timeout);
    return (result > 0 && FD_ISSET(STDOUT_FILENO, &write_fds));
}

void *ui_thread_func(void *arg) {
    while (1) {
        // 检查串口是否可写
        if (!is_stdout_ready()) {
            LOG_WARNING("串口不可写，跳过UI更新");
            usleep(500000);
            continue;
        }
        
        // 快速获取数据
        long local_played_bytes;
        long local_current_volume;
        int local_track_index;
        bool local_pause_flag;
        float local_playback_speed;
        
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        pthread_mutex_lock(&mutex);
        local_played_bytes = played_bytes;
        local_current_volume = current_volume;
        local_track_index = track_index;
        local_pause_flag = pause_flag;
        local_playback_speed = playback_speed;
        pthread_mutex_unlock(&mutex);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        
        // 计算当前播放秒数
        int cur_sec = 0, total_sec = 0;
        if (wav_header.byte_rate > 0) {
            cur_sec = local_played_bytes / wav_header.byte_rate;
            total_sec = wav_header.sub_chunk2_size / wav_header.byte_rate;
        }

        draw_ui(cur_sec, total_sec, local_current_volume, local_track_index,
                wav_file_count, wav_files, local_pause_flag, local_playback_speed);

        usleep(1000000);
    }
    LOG_INFO("UI线程结束");
    return NULL;
}