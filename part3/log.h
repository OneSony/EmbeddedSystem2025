#pragma once

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>

// 日志级别枚举
typedef enum {
    LOG_USER_OPERATION,    // 用户操作
    LOG_INFO,             // 信息
    LOG_WARNING,          // 警告  
    LOG_ERROR             // 错误
} log_level_t;

// 日志结构体
typedef struct {
    FILE *log_file;
    pthread_mutex_t log_mutex;
} logger_t;

// 全局日志对象
extern logger_t g_logger;

// 函数声明
int init_logger(const char *log_filename);
void close_logger(void);
void write_log(log_level_t level, const char *format, ...);

// 便利宏定义
#define LOG_USER(format, ...) write_log(LOG_USER_OPERATION, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) write_log(LOG_INFO, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) write_log(LOG_WARNING, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) write_log(LOG_ERROR, format, ##__VA_ARGS__) 