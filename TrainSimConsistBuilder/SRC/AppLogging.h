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

    enum class LogChannel
    {
        General = 0,
        Shape,
        Stock,
        Texture,
        Count
    };

    class AppLogger
    {
    public:
        static void Initialize();
        static void Shutdown();

        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        static bool OpenLogsFolderInExplorer();
        static bool OpenLogInExplorer();

        static void Log(LogLevel level, const char* file, int line, const char* func, const char* format, ...);
        static void LogW(LogLevel level, const char* file, int line, const char* func, const wchar_t* format, ...);

        static void LogToChannel(LogChannel channel, LogLevel level, const char* file, int line, const char* func, const char* format, ...);
        static void LogToChannelW(LogChannel channel, LogLevel level, const char* file, int line, const char* func, const wchar_t* format, ...);

        static std::wstring GetLogsDirectory();
        static std::wstring GetLogFilePath(LogChannel channel = LogChannel::General);
        static void Flush();

    private:
        static void OpenAllLogFilesInternal();
        static void CloseAllLogFilesInternal();
        static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pExPtrs);
        static void WriteCrashReport(EXCEPTION_POINTERS* pExPtrs);

        static std::recursive_mutex s_logMutex;
        static HANDLE s_hLogFiles[(int)LogChannel::Count];
        static std::wstring s_logFilePaths[(int)LogChannel::Count];
        static std::wstring s_logsDir;
        static bool s_initialized;
        static bool s_enabled;
        static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter;
    };

    class ScopedLogTimer
    {
    public:
        ScopedLogTimer(const char* name, const char* file, int line, const char* func)
            : m_name(name), m_file(file), m_line(line), m_func(func), m_start(GetTickCount64())
        {
            AppLogger::Log(LogLevel::Debug, m_file, m_line, m_func, "[START] %s", m_name);
        }
        ~ScopedLogTimer()
        {
            ULONGLONG elapsed = GetTickCount64() - m_start;
            AppLogger::Log(LogLevel::Debug, m_file, m_line, m_func, "[END] %s (Elapsed: %llums)", m_name, elapsed);
        }
    private:
        const char* m_name;
        const char* m_file;
        int m_line;
        const char* m_func;
        ULONGLONG m_start;
    };
}

// General Log Macros (Writes to AppLog.txt)
#define LOG_DEBUG(fmt, ...)        AppLogging::AppLogger::Log(AppLogging::LogLevel::Debug,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)         AppLogging::AppLogger::Log(AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)         AppLogging::AppLogger::Log(AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)        AppLogging::AppLogger::Log(AppLogging::LogLevel::Error,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)        AppLogging::AppLogger::Log(AppLogging::LogLevel::Fatal,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_DEBUG_W(fmt, ...)      AppLogging::AppLogger::LogW(AppLogging::LogLevel::Debug,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_INFO_W(fmt, ...)       AppLogging::AppLogger::LogW(AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_WARN_W(fmt, ...)       AppLogging::AppLogger::LogW(AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_ERROR_W(fmt, ...)      AppLogging::AppLogger::LogW(AppLogging::LogLevel::Error,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_FATAL_W(fmt, ...)      AppLogging::AppLogger::LogW(AppLogging::LogLevel::Fatal,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// Dedicated Shape Diagnostics (Writes to ShapeDiagnostics.log)
#define LOG_SHAPE(fmt, ...)        AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Shape,   AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_SHAPE_W(fmt, ...)      AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Shape,  AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_SHAPE_WARN(fmt, ...)   AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Shape,   AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_SHAPE_WARN_W(fmt, ...) AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Shape,  AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_SHAPE_ERROR(fmt, ...)  AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Shape,  AppLogging::LogLevel::Error,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_SHAPE_ERROR_W(fmt, ...) AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Shape, AppLogging::LogLevel::Error,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// Dedicated Stock Diagnostics (Writes to StockDiagnostics.log)
#define LOG_STOCK(fmt, ...)        AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Stock,   AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_STOCK_W(fmt, ...)      AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Stock,  AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_STOCK_WARN(fmt, ...)   AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Stock,   AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_STOCK_WARN_W(fmt, ...) AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Stock,  AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_STOCK_ERROR(fmt, ...)  AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Stock,  AppLogging::LogLevel::Error,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_STOCK_ERROR_W(fmt, ...) AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Stock, AppLogging::LogLevel::Error,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// Dedicated Texture Diagnostics (Writes to TextureDiagnostics.log)
#define LOG_TEXTURE(fmt, ...)      AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Texture, AppLogging::LogLevel::Info,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_TEXTURE_W(fmt, ...)    AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Texture, AppLogging::LogLevel::Info,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_TEXTURE_WARN(fmt, ...) AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Texture, AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_TEXTURE_WARN_W(fmt, ...) AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Texture, AppLogging::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_TEXTURE_ERROR(fmt, ...) AppLogging::AppLogger::LogToChannel(AppLogging::LogChannel::Texture, AppLogging::LogLevel::Error,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOG_TEXTURE_ERROR_W(fmt, ...) AppLogging::AppLogger::LogToChannelW(AppLogging::LogChannel::Texture, AppLogging::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#define LOG_SCOPE(name)            AppLogging::ScopedLogTimer _scopeLogTimer(name, __FILE__, __LINE__, __FUNCTION__)
