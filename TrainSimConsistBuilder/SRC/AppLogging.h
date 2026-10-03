#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>

namespace AppLogging
{
    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    class AppLogger
    {
    public:
        static void Initialize();
        static void Shutdown();

        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        static bool OpenLogInExplorer();

        static void Log(LogLevel level, const char* file, int line, const char* func, const char* format, ...);
        static void LogW(LogLevel level, const char* file, int line, const char* func, const wchar_t* format, ...);

        static std::wstring GetLogFilePath();
        static void Flush();

    private:
        static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pExPtrs);
        static void WriteCrashReport(EXCEPTION_POINTERS* pExPtrs);

        static std::mutex s_logMutex;
        static HANDLE s_hLogFile;
        static std::wstring s_logFilePath;
        static bool s_initialized;
        static bool s_enabled;
        static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter;
    };
}

#define LOG_DEBUG(fmt, ...)   AppLogging::AppLogger::Log(AppLogging::LogLevel::Debug,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    AppLogging::AppLogger::Log(AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    AppLogging::AppLogger::Log(AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)   AppLogging::AppLogger::Log(AppLogging::LogLevel::Error,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)   AppLogging::AppLogger::Log(AppLogging::LogLevel::Fatal,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_INFO_W(fmt, ...)  AppLogging::AppLogger::LogW(AppLogging::LogLevel::Info,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_ERROR_W(fmt, ...) AppLogging::AppLogger::LogW(AppLogging::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
