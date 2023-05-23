#include "log.h"

char *EM_logLevelGet(const int level){  // 得到当前输入等级level的字符串
    if(level == LOGLEVEL_DEBUG){
        return (char*)"DEBUG";
    }else if (level == LOGLEVEL_INFO ){
        return (char*)"INFO";
    }else if (level == LOGLEVEL_WARN ){
        return (char*)"WARN";
    }else if (level == LOGLEVEL_ERROR ){
        return (char*)"ERROR";
    }else{
        return (char*)"UNKNOWN";
    }
    
}

void getTime(char timeBuf[]) {
    time_t timep;
    time(&timep);
    struct tm nowTime;
    localtime_r(&timep, &nowTime);
    strftime(timeBuf, strlen(timeBuf), "%Y-%m-%d-%H:%M:%S", &nowTime);
}

void EM_log(const int level, const char* fun, const int line, const char *fmt, ...){ // 日志输出函数
    #ifdef OPEN_LOG     // 判断开关
    va_list arg;
    va_start(arg, fmt);
    char buf[1024];     // 创建缓存字符数组
    vsnprintf(buf, sizeof(buf), fmt, arg);          // 赋值 ftm 格式的 arg 到 buf
    va_end(arg);   
    if(level >= LOG_LEVEL){                         // 判断当前日志等级，与程序日志等级状态对比
        // printf("[%s]\t[%s %d]: %s \n", EM_logLevelGet(level), fun, line, buf);
		char timeBuf[64];
        memset(timeBuf, '0', sizeof timeBuf);
		getTime(timeBuf);
        FILE *logFile;
        logFile = fopen("./log/log.txt", "a");
        fprintf(logFile, "[%s]\t[%s]\t[%s %d]: %s \n", timeBuf, EM_logLevelGet(level), fun, line, buf);
        fclose(logFile);
    }  
    #endif
}