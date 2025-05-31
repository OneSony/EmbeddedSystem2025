#include "main.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

// 获取均衡器预设名称
const char* get_preset_name(eq_preset_t preset) {
    switch (preset) {
        case EQ_PRESET_FLAT: return "Flat";
        case EQ_PRESET_BASS_BOOST: return "Bass Boost";
        case EQ_PRESET_TREBLE_BOOST: return "Treble Boost";
        case EQ_PRESET_VOCAL: return "Vocal";
        case EQ_PRESET_CLASSICAL: return "Classical";
        case EQ_PRESET_ROCK: return "Rock";
        case EQ_PRESET_POP: return "Pop";
        default: return "Custom";
    }
}

// 绘制均衡器频段条形图
void draw_eq_bar(double gain_db, int bar_width) {
    int max_gain = 12; // 最大增益±12dB
    int center = bar_width / 2;
    int pos = center + (int)(gain_db * center / max_gain);
    
    if (pos < 0) pos = 0;
    if (pos >= bar_width) pos = bar_width - 1;
    
    printf("[");
    for (int i = 0; i < bar_width; i++) {
        if (i == center) {
            printf("|"); // 中心线
        } else if ((gain_db > 0 && i > center && i <= pos) || 
                   (gain_db < 0 && i < center && i >= pos)) {
            printf("#");
        } else {
            printf(" ");
        }
    }
    printf("]");
}

// 绘制UI函数
void draw_ui(int cur_sec, int total_sec, int volume, int track_index, int wav_file_count, char **wav_files, int pause_flag) {
    printf("\033[H\033[J"); // 清屏并移动光标到左上角

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

    // 均衡器状态
    printf("Equalizer: [%s] - %s\n", 
           equalizer.enabled ? "ON " : "OFF", 
           get_preset_name(equalizer.current_preset));
    
    // 均衡器频段显示
    if (equalizer.enabled) {
        const char* band_names[] = {"Bass", "L-Mid", "Mid", "H-Mid", "Treble"};
        printf("EQ Bands:\n");
        
        for (int i = 0; i < NUM_EQ_BANDS; i++) {
            printf("  %6s: ", band_names[i]);
            draw_eq_bar(equalizer.gains[i], 20);
            printf(" %+5.1f dB\n", equalizer.gains[i]);
        }
    }

    // 曲目列表
    printf("Tracklist:\n");
    for (int i = 0; i < wav_file_count; ++i) {
        if (i == track_index) printf(" > %s\n", wav_files[i]);
        else printf("   %s\n", wav_files[i]);
    }
    
    printf("\nControls:\n");
    printf("[n] Next  [b] Prev  [f] Forward  [r] Rewind  [p] Pause  [q] Quit\n");
    printf("[e] Toggle EQ  [0-6] EQ Presets\n");
    printf("EQ Bands: [z/Z] Bass  [x/X] L-Mid  [c/C] Mid  [v/V] H-Mid  [g/G] Treble\n");
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
        draw_ui(cur_sec, total_sec, current_volume, track_index, wav_file_count, wav_files, pause_flag);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        pthread_mutex_unlock(&mutex);
        usleep(200000); //0.2秒更新一次UI
    }
    return NULL;
}