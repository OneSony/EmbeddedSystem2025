# 第五组 大作业Part3报告

<<<<<<< HEAD

=======

>>>>>>> 9d404ef73d60ebd0484c5deb209f01b7ed291e80
>>>>>>> 陈敬文 李子睿 刘云
>>>>>>>
>>>>>>
>>>>>
>>>>
>>>
>>

## 实验过程

### 1. 环境配置

<<<<<<< HEAD

=======

>>>>>>> 9d404ef73d60ebd0484c5deb209f01b7ed291e80
>>>>>>> TODO 所需库
>>>>>>>
>>>>>>
>>>>>
>>>>
>>>
>>

### 2. 程序

### 2.1. 程序结构

<<<<<<< HEAD

TODO ly

### 2.2. 上一曲下一曲

TODO ly

### 2.3. 快进快退

TODO ly

### 2.4. 暂停继续

TODO ly

### 2.5. 多种倍速播放

本部分实现了基于**WSOLA（Waveform Similarity Overlap-Add）**的时域变速算法，主要用于音频变速不变调。下面详细说明其实现过程：

---

#### 1. 数据结构

* **WsolaConfig** ：配置参数，包括帧长（frame_size）、重叠长度（overlap_size）、变速比（speed_ratio）。
* **WsolaState** ：运行状态，包含当前帧、前一帧、输出缓冲、通道数、采样位数等。

---

#### 2. PCM与浮点转换

* `read_sample`：将 interleaved PCM 数据（16/24/32位）转换为 float，便于后续处理。
* `write_sample`：将 float 数据转换回 PCM 格式，写入输出缓冲。

---

#### 3. 寻找最佳重叠点

* `find_best_offset`：
  * 先用较大步长（coarse_step）粗略搜索，找到相关性最大的重叠起点；
  * 再在该点附近用步长1精细搜索，得到最佳重叠偏移（offset）。
  * 相关性通过重叠区的点乘计算，最大相关性即为最佳拼接点。

---

#### 4. 状态初始化

* `wsola_state_init`：
  * 分配当前帧、前一帧、输出缓冲的内存；
  * 前一帧初始化为全零，便于首帧处理。

---

#### 5. 变速处理主流程（核心）

* `wsola_state_process`：

  1. **参数准备** ：重置输入/输出索引，计算每次处理的chunk大小（frame_size - overlap_size）。
  2. **内存检查** ：如输出缓冲不够大则扩容。
  3. **分帧处理** ：

  * 将输入PCM数据分帧，转换为浮点，填入curr_frame。
  * 对每一帧：
    * 调用 `find_best_offset`，在prev_frame中寻找与curr_frame重叠区最相似的拼接点。
    * 计算输出起点 `ostart`（根据变速比调整）。
    * 将当前帧与前一帧的重叠部分相加，非重叠部分直接赋值，结果写入输出缓冲fout。
    * 更新prev_frame为curr_frame，推进输入/输出索引。

  1. **输出限制** ：如输出帧数超限则截断。
  2. **浮点转PCM** ：将fout中的浮点数据转换为PCM，写入out_bytes。
  3. **返回实际输出帧数** 。

---

#### 6. 资源释放

* `wsola_state_free`：释放所有分配的内存。

---

#### 7. 算法特点

* **WSOLA**的核心是“重叠-相似拼接”，通过寻找最佳重叠点，拼接时减少音质失真。
* 变速时，输出帧的间隔根据speed_ratio调整，实现变速不变调。
* 支持多通道和多种采样位宽。

### 2.6. 滤波器

TODO cjw

### 3. 使用说明

```bash
./music_app -m <dir_path>
```

运行后程序会自动读取目录下所有的wav文件, 并加入到tracklist. 用户可以通过 `ctrl+C`或 `q`键退出程序. 其他操作见用户界面提示.
===============================================================================================================

TODO ly

### 2.2. 上一曲下一曲

TODO ly

### 2.3. 快进快退

TODO ly

### 2.4. 暂停继续

TODO ly

### 2.5. 多种倍速播放

TODO lzr

### 2.6. 滤波器

TODO cjw

### 3. 使用说明

```bash
./music_app -m <dir_path>
```

运行后程序会自动读取目录下所有的wav文件, 并加入到tracklist. 用户可以通过 `ctrl+C`或 `q`键退出程序. 其他操作见用户界面提示.

>>>>>>> 9d404ef73d60ebd0484c5deb209f01b7ed291e80
>>>>>>>
>>>>>>
>>>>>
>>>>
>>>
>>

## 实验结果

![结果图](./fig/result.png)

## 实验心得

### 单片机算力不足

<<<<<<< HEAD

TODO

### 线程保护

TODO

### TODO

TODO

## wav文件来源

https://samplelib.com/zh/sample-wav.html
TODO
====

TODO

### 线程保护

TODO

### TODO

TODO

## wav文件来源

https://samplelib.com/zh/sample-wav.html
TODO

>>>>>>> 9d404ef73d60ebd0484c5deb209f01b7ed291e80
>>>>>>>
>>>>>>
>>>>>
>>>>
>>>
>>
