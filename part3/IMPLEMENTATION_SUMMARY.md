# FIR滤波器音频均衡器实现总结

## 项目概述

本项目在现有的音乐播放器基础上，实现了一个基于FIR滤波器的5频段音频均衡器，能够对不同频率的声音应用不同的增益，实现多种音效（如低音更响亮或高音更响亮），完全不调用多媒体库函数。

## 技术实现架构

### 1. 核心组件

#### 1.1 FIR滤波器设计 (`equalizer.c`)
- **滤波器阶数**: 64阶
- **窗函数**: 汉明窗 (Hamming Window)
- **设计方法**: 窗函数法
- **滤波器类型**:
  - 低音频段: 低通滤波器 (截止频率250Hz)
  - 高音频段: 高通滤波器 (截止频率4000Hz)  
  - 中频频段: 带通滤波器 (根据频段范围设计)

#### 1.2 数据结构定义 (`main.h`)
```c
// FIR滤波器结构
typedef struct {
    double coefficients[FIR_FILTER_ORDER + 1];  // 滤波器系数
    double delay_line[FIR_FILTER_ORDER + 1];    // 延迟线
    int order;                                   // 滤波器阶数
    double center_freq;                          // 中心频率
    double gain;                                 // 增益
} fir_filter_t;

// 均衡器结构
typedef struct {
    fir_filter_t bands[NUM_EQ_BANDS];           // 各频段的FIR滤波器
    double gains[NUM_EQ_BANDS];                 // 各频段增益 (dB)
    eq_preset_t current_preset;                 // 当前预设
    bool enabled;                               // 均衡器是否启用
    int sample_rate;                            // 采样率
} audio_equalizer_t;
```

### 2. 频段划分

| 频段 | 频率范围 | 音频特性 | 滤波器类型 |
|------|----------|----------|------------|
| Bass | 60-250 Hz | 低音、鼓声、低音吉他 | 低通滤波器 |
| Low-Mid | 250-500 Hz | 低中音、人声低频 | 带通滤波器 |
| Mid | 500-2000 Hz | 中音、人声主频 | 带通滤波器 |
| High-Mid | 2000-4000 Hz | 高中音、乐器谐波 | 带通滤波器 |
| Treble | 4000-16000 Hz | 高音、细节、空气感 | 高通滤波器 |

### 3. 预设模式设计

```c
static const double eq_presets[EQ_NUM_PRESETS][NUM_EQ_BANDS] = {
    {0.0, 0.0, 0.0, 0.0, 0.0},      // Flat - 平衡
    {6.0, 3.0, 0.0, -1.0, -2.0},    // Bass Boost - 低音增强
    {-2.0, -1.0, 0.0, 3.0, 6.0},    // Treble Boost - 高音增强
    {-2.0, -1.0, 4.0, 3.0, -1.0},   // Vocal - 人声增强
    {3.0, 1.0, -1.0, 1.0, 4.0},     // Classical - 古典音乐
    {4.0, 1.0, -2.0, 1.0, 5.0},     // Rock - 摇滚音乐
    {2.0, 1.0, -0.5, 2.0, 3.0}      // Pop - 流行音乐
};
```

## 关键算法实现

### 1. FIR滤波器系数计算

#### 低通滤波器设计
```c
void design_lowpass_filter(double *coefficients, int order, double cutoff_freq, int sample_rate) {
    double omega_c = 2.0 * M_PI * cutoff_freq / sample_rate;
    int M = order;
    
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
```

#### 带通滤波器设计
```c
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
```

### 2. 实时音频处理

#### 样本级滤波处理
```c
double fir_filter_process_sample(fir_filter_t *filter, double input) {
    // 移动延迟线
    for (int i = filter->order; i > 0; i--) {
        filter->delay_line[i] = filter->delay_line[i-1];
    }
    
    // 新样本输入
    filter->delay_line[0] = input;
    
    // 卷积运算
    double output = 0.0;
    for (int i = 0; i <= filter->order; i++) {
        output += filter->coefficients[i] * filter->delay_line[i];
    }
    
    return output * filter->gain;
}
```

#### 多频段并行处理
```c
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
```

## 集成到音频播放流程

### 1. 初始化阶段 (`play.c`)
```c
int init_pcm() {
    // ... PCM初始化代码 ...
    
    // 初始化均衡器
    equalizer_init(&equalizer, wav_header.sample_rate);
    
    // ... 后续代码 ...
}
```

### 2. 播放线程处理 (`play.c`)
```c
void *playback_thread_func(void *arg) {
    while(1) {
        // 读取音频数据
        int read_bytes = fread(buff, 1, buffer_size, fp);
        
        // 对音频数据应用均衡器处理（仅支持16位音频）
        if (wav_header.bits_per_sample == 16 && equalizer.enabled) {
            int num_samples = read_bytes / wav_header.block_align;
            equalizer_process_audio(&equalizer, (int16_t *)buff, num_samples, wav_header.num_channels);
        }
        
        // 输出到声卡
        // ...
    }
}
```

## 用户交互界面

### 1. 键盘控制 (`control.c`)
- `e`: 开启/关闭均衡器
- `0-6`: 切换预设模式
- `z/Z, x/X, c/C, v/V, g/G`: 手动调节各频段增益

### 2. 可视化显示 (`ui.c`)
- 实时显示均衡器状态
- 频段增益条形图显示
- 当前预设模式显示

## 技术特点

### 1. 纯算法实现
- 不使用任何多媒体库函数
- 完全基于数学计算的FIR滤波器设计
- 手动实现音频信号处理

### 2. 实时处理
- 在音频播放过程中实时处理
- 低延迟处理保证音频连续性
- 动态调节均衡器参数

### 3. 高质量音效
- 64阶FIR滤波器确保良好的频率响应
- 汉明窗函数减少频谱泄漏
- 防失真限幅处理

### 4. 用户友好
- 直观的键盘控制
- 实时视觉反馈
- 多种预设模式

## 性能考量

### 1. 计算复杂度
- 每个样本需要进行5次64阶FIR卷积运算
- 总计算量：O(5 × 64 × 采样率)
- 对于44.1kHz采样率：约14M运算/秒

### 2. 内存使用
- 每个滤波器：65个双精度浮点数（延迟线 + 系数）
- 5个频段总计：约2.6KB
- 内存使用量较小，适合实时处理

### 3. 优化策略
- 使用固定点运算可进一步优化性能
- 并行处理多个频段
- 预计算滤波器系数

## 文件结构

```
part3/
├── main.h              # 头文件，包含所有结构定义
├── main.c              # 主程序入口
├── equalizer.c         # FIR均衡器核心实现
├── play.c              # 音频播放和均衡器集成
├── control.c           # 键盘控制和均衡器控制
├── ui.c                # 用户界面和均衡器显示
├── global.c            # 全局变量定义
├── makefile            # 编译配置
├── README_EQUALIZER.md # 功能说明文档
└── test_equalizer.sh   # 测试脚本
```

## 编译和使用

```bash
# 编译
make clean && make

# 运行测试
./test_equalizer.sh

# 手动运行
./music_app -m /path/to/music/directory
```

## 总结

本项目成功实现了一个完整的基于FIR滤波器的音频均衡器，具有以下优势：

1. **技术先进**: 使用数字信号处理理论设计FIR滤波器
2. **功能完整**: 支持5频段独立调节和7种预设模式  
3. **实时性好**: 在音频播放过程中实时处理，延迟极低
4. **易于使用**: 直观的键盘控制和可视化界面
5. **纯算法**: 不依赖多媒体库，完全自主实现

该实现展示了数字信号处理在实际音频应用中的应用，是嵌入式系统和音频处理技术的良好结合。 