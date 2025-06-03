#include "main.h"

// 均衡器预设的增益值 (dB)
static const double eq_presets[EQ_NUM_PRESETS][NUM_EQ_BANDS] = {
    // FLAT - 所有频段增益为0
    {0.0, 0.0, 0.0, 0.0, 0.0},
    
    // BASS_BOOST - 强烈增强低音，压制高音
    {16.0, 8.0, 0.0, -8.0, -16.0},
    
    // TREBLE_BOOST - 强烈增强高音，压制低音
    {-16.0, -8.0, 0.0, 8.0, 16.0},

    // VOICE_BOOST - 增强人声，压制其他频段
    {-8.0, -8.0, 16.0, -8.0, -8.0}
};

// 各频段的中心频率和带宽
static const struct {
    double low_freq;
    double high_freq;
} eq_bands_freq[NUM_EQ_BANDS] = {
    {60, 250},      // BASS
    {250, 500},     // LOW_MID  
    {500, 2000},    // MID
    {2000, 4000},   // HIGH_MID
    {4000, 16000}   // TREBLE
};

// 设计低通滤波器系数
void design_lowpass_filter(double *coefficients, int order, double cutoff_freq, int sample_rate) {
    double omega_c = 2.0 * M_PI * cutoff_freq / sample_rate;
    int M = order;
    
    // 使用窗函数法设计FIR低通滤波器
    for (int n = 0; n <= M; n++) {
        double h_ideal;
        if (n == M/2) {
            h_ideal = omega_c / M_PI;
        } else {
            h_ideal = sin(omega_c * (n - M/2.0)) / (M_PI * (n - M/2.0));
        }
        
        // 汉明窗
        double window = 0.54 - 0.46 * cos(2.0 * M_PI * n / M);
        coefficients[n] = h_ideal * window;
    }
}

// 设计高通滤波器系数
void design_highpass_filter(double *coefficients, int order, double cutoff_freq, int sample_rate) {
    double omega_c = 2.0 * M_PI * cutoff_freq / sample_rate;
    int M = order;
    
    // 先设计低通滤波器
    for (int n = 0; n <= M; n++) {
        double h_ideal;
        if (n == M/2) {
            h_ideal = omega_c / M_PI;
        } else {
            h_ideal = sin(omega_c * (n - M/2.0)) / (M_PI * (n - M/2.0));
        }
        
        // 汉明窗
        double window = 0.54 - 0.46 * cos(2.0 * M_PI * n / M);
        coefficients[n] = h_ideal * window;
    }
    
    // 转换为高通滤波器：h_hp[n] = δ[n] - h_lp[n]
    for (int n = 0; n <= M; n++) {
        if (n == M/2) {
            coefficients[n] = 1.0 - coefficients[n];
        } else {
            coefficients[n] = -coefficients[n];
        }
    }
}

// 设计带通滤波器系数
void design_bandpass_filter(double *coefficients, int order, double low_freq, double high_freq, int sample_rate) {
    double omega_l = 2.0 * M_PI * low_freq / sample_rate;
    double omega_h = 2.0 * M_PI * high_freq / sample_rate;
    int M = order;
    
    for (int n = 0; n <= M; n++) {
        double h_ideal;
        if (n == M/2) {
            h_ideal = (omega_h - omega_l) / M_PI;
        } else {
            double t = n - M/2.0;
            h_ideal = (sin(omega_h * t) - sin(omega_l * t)) / (M_PI * t);
        }
        
        // 汉明窗
        double window = 0.54 - 0.46 * cos(2.0 * M_PI * n / M);
        coefficients[n] = h_ideal * window;
    }
}

// 初始化FIR滤波器
void fir_filter_init(fir_filter_t *filter, double center_freq, double gain_db, int sample_rate, int order) {
    filter->order = order;
    filter->center_freq = center_freq;
    filter->gain = pow(10.0, gain_db / 20.0); // 将dB转换为线性增益
    
    // 清零延迟线
    memset(filter->delay_line, 0, sizeof(filter->delay_line));
    
    // 根据频率范围设计相应的滤波器
    if (center_freq <= 250) {
        // 低音频段 - 低通滤波器
        design_lowpass_filter(filter->coefficients, order, 250, sample_rate);
    } else if (center_freq >= 4000) {
        // 高音频段 - 高通滤波器  
        design_highpass_filter(filter->coefficients, order, 4000, sample_rate);
    } else {
        // 中频段 - 带通滤波器
        double low_freq, high_freq;
        if (center_freq <= 375) {        // 250-500 Hz
            low_freq = 250; high_freq = 500;
        } else if (center_freq <= 1250) {  // 500-2000 Hz
            low_freq = 500; high_freq = 2000;
        } else {                         // 2000-4000 Hz
            low_freq = 2000; high_freq = 4000;
        }
        design_bandpass_filter(filter->coefficients, order, low_freq, high_freq, sample_rate);
    }
}

// FIR滤波器处理单个样本
double fir_filter_process_sample(fir_filter_t *filter, double input) {
    // 移动延迟线
    for (int i = filter->order; i > 0; i--) {
        filter->delay_line[i] = filter->delay_line[i-1];
    }
    
    // 新样本输入
    filter->delay_line[0] = input;
    
    // 计算输出
    double output = 0.0;
    for (int i = 0; i <= filter->order; i++) {
        output += filter->coefficients[i] * filter->delay_line[i];
    }
    
    return output * filter->gain;
}

// 初始化均衡器
void equalizer_init(audio_equalizer_t *eq, int sample_rate) {
    eq->sample_rate = sample_rate;
    eq->enabled = true;
    eq->current_preset = EQ_PRESET_FLAT;
    
    // 初始化各频段增益为0
    for (int i = 0; i < NUM_EQ_BANDS; i++) {
        eq->gains[i] = 0.0;
    }
    
    // 初始化各频段的FIR滤波器
    double center_freqs[NUM_EQ_BANDS] = {155, 375, 1250, 3000, 10000}; // 各频段中心频率
    
    for (int i = 0; i < NUM_EQ_BANDS; i++) {
        fir_filter_init(&eq->bands[i], center_freqs[i], eq->gains[i], sample_rate, FIR_FILTER_ORDER);
    }
    
    LOG_INFO("均衡器初始化完成，采样率: %d Hz", sample_rate);
}

// 设置均衡器预设
void equalizer_set_preset(audio_equalizer_t *eq, eq_preset_t preset) {
    if (preset >= EQ_NUM_PRESETS) {
        LOG_ERROR("无效的均衡器预设: %d", preset);
        return;
    }
    
    eq->current_preset = preset;
    
    // 应用预设的增益值
    for (int i = 0; i < NUM_EQ_BANDS; i++) {
        eq->gains[i] = eq_presets[preset][i];
        eq->bands[i].gain = pow(10.0, eq->gains[i] / 20.0);
    }
    
    const char* preset_names[] = {"Flat", "Bass Boost", "Treble Boost", "Voice Boost"};
    LOG_INFO("均衡器切换到预设: %s", preset_names[preset]);
}

// 处理音频数据
void equalizer_process_audio(audio_equalizer_t *eq, int16_t *audio_data, int num_samples, int channels) {
    if (!eq->enabled) return;
    
    for (int sample = 0; sample < num_samples; sample++) {
        for (int ch = 0; ch < channels; ch++) {
            int index = sample * channels + ch;
            
            // 转换为double进行处理，范围[-1.0, 1.0]
            double input = (double)audio_data[index] / 32768.0;
            double output = 0.0;
            
            // 各频段并行处理后叠加
            for (int band = 0; band < NUM_EQ_BANDS; band++) {
                output += fir_filter_process_sample(&eq->bands[band], input);
            }
            
            // 限制输出范围并转换回int16_t
            output = fmax(-1.0, fmin(1.0, output));
            audio_data[index] = (int16_t)(output * 32767.0);
        }
    }
}

// 清理均衡器资源
void equalizer_cleanup(audio_equalizer_t *eq) {
    // 重置所有滤波器的延迟线
    for (int i = 0; i < NUM_EQ_BANDS; i++) {
        memset(eq->bands[i].delay_line, 0, sizeof(eq->bands[i].delay_line));
    }
    
    // 清理资源（如果有动态分配的内存）
    memset(eq, 0, sizeof(audio_equalizer_t));
    LOG_INFO("均衡器资源清理完成");
} 