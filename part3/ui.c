#include "main.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

// 绘制UI函数

void draw_ui(int cur_sec, int total_sec, int volume, int track_index, int wav_file_count, char **wav_files, int pause_flag) {
    printf("\033[H"); // 只移动光标到左上角，不清屏

    // 统一进度条和音量条长度
    int bar_len = 40;

    // 进度条
    printf("Progress: [");
    int pos = (total_sec > 0) ? (cur_sec * bar_len) / total_sec : 0;
    for (int i = 0; i < bar_len; ++i) {
        if (i < pos) printf("#");
        else printf(" ");
    }
    printf("] %d/%d sec\n", cur_sec, total_sec);

    // 音量条
    printf("Volume:   [");
    int vol_bar = (volume * bar_len) / 100;
    for (int i = 0; i < bar_len; ++i) {
        if (i < vol_bar) printf("#");
        else printf(" ");
    }
    printf("] %3d%%\n", volume);

    // 播放/暂停状态
    printf("Status:   [%s]\n", pause_flag ? "Paused" : "Playing");
    // 播放速率
    printf("Speed:    [%.1fx]\n", playback_speed);

    // 曲目列表
    printf("Tracklist:\n");
    for (int i = 0; i < wav_file_count; ++i) {
        if (i == track_index) printf(" > %s\n", wav_files[i]);
        else printf("   %s\n", wav_files[i]);
    }
    printf("\n[n] Next  [b] Prev  [f] Forward  [r] Rewind  [s] Speed  [p] Pause/Resume [q] Quit\n");
    fflush(stdout);
}

void *ui_thread_func(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex);

        // 计算当前播放秒数
        int cur_sec = 0, total_sec = 0;
        if (wav_header.byte_rate > 0) {
            cur_sec = played_bytes / wav_header.byte_rate;
            total_sec = wav_header.sub_chunk2_size / wav_header.byte_rate;
        }

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 防止中断
        draw_ui(cur_sec, total_sec, current_volume, track_index,
                wav_file_count, wav_files, playback_speed);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        pthread_mutex_unlock(&mutex);
        usleep(200000); //0.2秒更新一次UI
    }
    return NULL;
}