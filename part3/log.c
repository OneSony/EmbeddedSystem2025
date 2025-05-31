#include "log.h"
#include <string.h>
#include <errno.h>

// 全局日志对象定义
logger_t g_logger = {NULL, PTHREAD_MUTEX_INITIALIZER};

// 获取日志级别字符串
static const char* get_log_level_string(log_level_t level) {
    switch (level) {
        case LOG_USER_OPERATION:
            return "USER_OP";
        case LOG_INFO:
            return "INFO";
        case LOG_WARNING:
            return "WARNING";
        case LOG_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

// 初始化日志系统
int init_logger(const char *log_filename) {
    pthread_mutex_lock(&g_logger.log_mutex);
    
    // 如果日志文件已经打开，先关闭
    if (g_logger.log_file != NULL) {
        fclose(g_logger.log_file);
    }
    
    // 打开日志文件（追加模式）
    g_logger.log_file = fopen(log_filename, "a");
    if (g_logger.log_file == NULL) {
        pthread_mutex_unlock(&g_logger.log_mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&g_logger.log_mutex);
    
    // 记录日志系统启动
    write_log(LOG_INFO, "音乐播放器日志系统启动");
    
    return 0;
}

// 关闭日志系统
void close_logger(void) {
    pthread_mutex_lock(&g_logger.log_mutex);
    
    if (g_logger.log_file != NULL) {
        // 记录日志系统关闭
        time_t now;
        struct tm *tm_info;
        char timestamp[20];
        
        time(&now);
        tm_info = localtime(&now);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(g_logger.log_file, "[%s] [INFO] 音乐播放器日志系统关闭\n", timestamp);
        fflush(g_logger.log_file);
        
        fclose(g_logger.log_file);
        g_logger.log_file = NULL;
    }
    
    pthread_mutex_unlock(&g_logger.log_mutex);
}

// 写入日志
void write_log(log_level_t level, const char *format, ...) {
    pthread_mutex_lock(&g_logger.log_mutex);
    
    if (g_logger.log_file == NULL) {
        pthread_mutex_unlock(&g_logger.log_mutex);
        return;
    }
    
    // 获取当前时间戳
    time_t now;
    struct tm *tm_info;
    char timestamp[20];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // 写入时间戳和日志级别
    fprintf(g_logger.log_file, "[%s] [%s] ", timestamp, get_log_level_string(level));
    
    // 写入日志内容
    va_list args;
    va_start(args, format);
    vfprintf(g_logger.log_file, format, args);
    va_end(args);
    
    fprintf(g_logger.log_file, "\n");
    fflush(g_logger.log_file); // 立即刷新到文件
    
    pthread_mutex_unlock(&g_logger.log_mutex);
} 