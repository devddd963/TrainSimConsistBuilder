#include "AppLogging.h"
#include "DatabaseManager.h"
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <chrono>
#include <iomanip>
#include <DbgHelp.h>
#include <shlwapi.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shlwapi.lib")

namespace AppLogging
{
    std::recursive_mutex AppLogger::s_logMutex;
    HANDLE AppLogger::s_hLogFiles[(int)LogChannel::Count] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
    std::wstring AppLogger::s_logFilePaths[(int)LogChannel::Count];
    std::wstring AppLogger::s_logsDir;
    bool AppLogger::s_initialized = false;
    bool AppLogger::s_enabled = true;
    LPTOP_LEVEL_EXCEPTION_FILTER AppLogger::s_prevFilter = nullptr;

    static const char* LogLevelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:                return "UNKN ";
        }
    }

    static const wchar_t* GetChannelFileName(LogChannel channel)
    {
        switch (channel)
        {
        case LogChannel::General: return L"AppLog.txt";
        case LogChannel::Shape:   return L"ShapeDiagnostics.log";
        case LogChannel::Stock:   return L"StockDiagnostics.log";
        case LogChannel::Texture: return L"TextureDiagnostics.log";
        default:                  return L"GeneralDiagnostics.log";
        }
    }

    static const char* GetChannelHeaderTitle(LogChannel channel)
    {
        switch (channel)
        {
        case LogChannel::General: return "TRAIN SIM CONSIST BUILDER v9.3.0 SESSION LOG";
        case LogChannel::Shape:   return "TRAIN SIM CONSIST BUILDER - SHAPE & 3D GEOMETRY DIAGNOSTICS";
        case LogChannel::Stock:   return "TRAIN SIM CONSIST BUILDER - STOCK (.ENG/.WAG) DIAGNOSTICS";
        case LogChannel::Texture: return "TRAIN SIM CONSIST BUILDER - TEXTURE & MATERIAL RESOLUTION TRACE";
        default:                  return "TRAIN SIM CONSIST BUILDER - DIAGNOSTIC LOG";
        }
    }

    void AppLogger::OpenAllLogFilesInternal()
    {
        wchar_t szExePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        std::wstring exePath = szExePath;
        size_t lastSlash = exePath.find_last_of(L"\\/");
        std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";

        std::wstring appDataDir = dir + L"AppData";
        CreateDirectoryW(appDataDir.c_str(), NULL);

        s_logsDir = appDataDir + L"\\Logs";
        CreateDirectoryW(s_logsDir.c_str(), NULL);

        SYSTEMTIME st;
        GetLocalTime(&st);

        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        GlobalMemoryStatusEx(&memStatus);
        DWORDLONG totalRamMb = memStatus.ullTotalPhys / (1024 * 1024);
        DWORDLONG availRamMb = memStatus.ullAvailPhys / (1024 * 1024);

        SYSTEM_INFO sysInfo;
        GetNativeSystemInfo(&sysInfo);

        DISPLAY_DEVICEW dd = { sizeof(dd) };
        std::wstring gpuName = L"Unknown GPU";
        if (EnumDisplayDevicesW(NULL, 0, &dd, 0))
        {
            gpuName = dd.DeviceString;
        }

        for (int i = 0; i < (int)LogChannel::Count; ++i)
        {
            if (s_hLogFiles[i] != INVALID_HANDLE_VALUE) continue;

            LogChannel ch = (LogChannel)i;
            s_logFilePaths[i] = s_logsDir + L"\\" + GetChannelFileName(ch);

            s_hLogFiles[i] = CreateFileW(
                s_logFilePaths[i].c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ,
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );

            if (s_hLogFiles[i] != INVALID_HANDLE_VALUE)
            {
                char header[1280];
                snprintf(header, sizeof(header),
                    "================================================================================\r\n"
                    "=== %s ===\r\n"
                    "=== Started: %04d-%02d-%02d %02d:%02d:%02d.%03d | PID: %lu | Arch: %s ===\r\n"
                    "=== System: %u CPU Cores | RAM: %llu MB Total, %llu MB Avail ===\r\n"
                    "=== Display Adapter: %ls ===\r\n"
                    "================================================================================\r\n\r\n",
                    GetChannelHeaderTitle(ch),
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    GetCurrentProcessId(),
#if defined(_WIN64)
                    "x64",
#else
                    "x86",
#endif
                    sysInfo.dwNumberOfProcessors,
                    totalRamMb, availRamMb,
                    gpuName.c_str());

                DWORD written = 0;
                WriteFile(s_hLogFiles[i], header, (DWORD)strlen(header), &written, NULL);
                FlushFileBuffers(s_hLogFiles[i]);
            }
        }
    }

    void AppLogger::CloseAllLogFilesInternal()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);

        char footer[256];
        snprintf(footer, sizeof(footer),
            "\r\n=== APPLICATION SESSION CLOSED NORMALLY: %04d-%02d-%02d %02d:%02d:%02d.%03d ===\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        for (int i = 0; i < (int)LogChannel::Count; ++i)
        {
            if (s_hLogFiles[i] != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile(s_hLogFiles[i], footer, (DWORD)strlen(footer), &written, NULL);
                FlushFileBuffers(s_hLogFiles[i]);
                CloseHandle(s_hLogFiles[i]);
                s_hLogFiles[i] = INVALID_HANDLE_VALUE;
            }
        }
    }

    static void CustomInvalidParameterHandler(
        const wchar_t* expression,
        const wchar_t* function,
        const wchar_t* file,
        unsigned int line,
        uintptr_t pReserved)
    {
        LOG_ERROR_W(L"[CRT_INVALID_PARAMETER] Expression: '%ls', Function: '%ls', File: '%ls', Line: %u",
            expression ? expression : L"n/a",
            function ? function : L"n/a",
            file ? file : L"n/a",
            line);
    }

    static void CustomPurecallHandler()
    {
        LOG_ERROR("[CRT_PURECALL] Pure virtual function call intercepted!");
    }

    void AppLogger::Initialize()
    {
        std::lock_guard<std::recursive_mutex> lock(s_logMutex);
        if (s_initialized) return;

        s_enabled = (DatabaseManager::GetSettingInt(L"EnableAppLogging", 1) != 0);

        s_prevFilter = SetUnhandledExceptionFilter(CrashFilter);
        _set_invalid_parameter_handler(CustomInvalidParameterHandler);
        _set_purecall_handler(CustomPurecallHandler);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);

        s_initialized = true;

        if (s_enabled)
        {
            OpenAllLogFilesInternal();
        }
    }

    void AppLogger::SetEnabled(bool enabled)
    {
        std::lock_guard<std::recursive_mutex> lock(s_logMutex);
        s_enabled = enabled;
        DatabaseManager::SetSettingInt(L"EnableAppLogging", enabled ? 1 : 0);

        if (enabled)
        {
            OpenAllLogFilesInternal();
            LOG_INFO("Diagnostic logging turned ON by user.");
        }
        else
        {
            LOG_INFO("Diagnostic logging turned OFF by user.");
            CloseAllLogFilesInternal();
        }
    }

    bool AppLogger::IsEnabled()
    {
        return s_enabled;
    }

    std::wstring AppLogger::GetLogsDirectory()
    {
        if (s_logsDir.empty())
        {
            wchar_t szExePath[MAX_PATH] = { 0 };
            GetModuleFileNameW(NULL, szExePath, MAX_PATH);
            std::wstring exePath = szExePath;
            size_t lastSlash = exePath.find_last_of(L"\\/");
            std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";
            s_logsDir = dir + L"AppData\\Logs";
        }
        return s_logsDir;
    }

    bool AppLogger::OpenLogsFolderInExplorer()
    {
        std::wstring logsDir = GetLogsDirectory();
        CreateDirectoryW(logsDir.c_str(), NULL);
        HINSTANCE hInst = ShellExecuteW(NULL, L"open", logsDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return ((INT_PTR)hInst > 32);
    }

    bool AppLogger::OpenLogInExplorer()
    {
        std::wstring generalLog = GetLogFilePath(LogChannel::General);
        if (PathFileExistsW(generalLog.c_str()))
        {
            HINSTANCE hInst = ShellExecuteW(NULL, L"open", generalLog.c_str(), NULL, NULL, SW_SHOWNORMAL);
            if ((INT_PTR)hInst > 32) return true;
        }
        return OpenLogsFolderInExplorer();
    }

    void AppLogger::Shutdown()
    {
        std::lock_guard<std::recursive_mutex> lock(s_logMutex);
        if (!s_initialized) return;

        if (s_prevFilter)
        {
            SetUnhandledExceptionFilter(s_prevFilter);
            s_prevFilter = nullptr;
        }

        CloseAllLogFilesInternal();

        SymCleanup(GetCurrentProcess());
        s_initialized = false;
    }

    std::wstring AppLogger::GetLogFilePath(LogChannel channel)
    {
        int idx = (int)channel;
        if (idx >= 0 && idx < (int)LogChannel::Count)
        {
            if (!s_logFilePaths[idx].empty())
                return s_logFilePaths[idx];
            return GetLogsDirectory() + L"\\" + GetChannelFileName(channel);
        }
        return GetLogsDirectory() + L"\\AppLog.txt";
    }

    void AppLogger::Flush()
    {
        std::lock_guard<std::recursive_mutex> lock(s_logMutex);
        for (int i = 0; i < (int)LogChannel::Count; ++i)
        {
            if (s_hLogFiles[i] != INVALID_HANDLE_VALUE)
            {
                FlushFileBuffers(s_hLogFiles[i]);
            }
        }
    }

    void AppLogger::Log(LogLevel level, const char* file, int line, const char* func, const char* format, ...)
    {
        if (!s_enabled) return;
        if (!s_initialized) Initialize();

        char msgBuf[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(msgBuf, sizeof(msgBuf), format, args);
        va_end(args);

        LogToChannel(LogChannel::General, level, file, line, func, "%s", msgBuf);
    }

    void AppLogger::LogW(LogLevel level, const char* file, int line, const char* func, const wchar_t* format, ...)
    {
        if (!s_enabled) return;
        if (!s_initialized) Initialize();

        wchar_t wMsgBuf[4096];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(wMsgBuf, _countof(wMsgBuf), _TRUNCATE, format, args);
        va_end(args);

        LogToChannelW(LogChannel::General, level, file, line, func, L"%ls", wMsgBuf);
    }

    void AppLogger::LogToChannel(LogChannel channel, LogLevel level, const char* file, int line, const char* func, const char* format, ...)
    {
        if (!s_enabled) return;
        if (!s_initialized) Initialize();

        int chIdx = (int)channel;
        if (chIdx < 0 || chIdx >= (int)LogChannel::Count) chIdx = 0;

        SYSTEMTIME st;
        GetLocalTime(&st);

        const char* baseFile = file;
        const char* p1 = strrchr(file, '\\');
        const char* p2 = strrchr(file, '/');
        if (p1 && p2) baseFile = (p1 > p2) ? (p1 + 1) : (p2 + 1);
        else if (p1) baseFile = p1 + 1;
        else if (p2) baseFile = p2 + 1;

        char msgBuf[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(msgBuf, sizeof(msgBuf), format, args);
        va_end(args);

        char lineBuf[5120];
        int len = snprintf(lineBuf, sizeof(lineBuf),
            "[%02d:%02d:%02d.%03d] [%s] [TID:%04lu] [%s:%d - %s] %s\r\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            LogLevelToString(level),
            GetCurrentThreadId(),
            baseFile, line, func,
            msgBuf
        );

        if (len > 0)
        {
            std::lock_guard<std::recursive_mutex> lock(s_logMutex);
            if (s_hLogFiles[chIdx] != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile(s_hLogFiles[chIdx], lineBuf, (DWORD)len, &written, NULL);
            }
            // If logged to a specialized diagnostic channel, also mirror warnings/errors to General AppLog
            if (chIdx != 0 && (level == LogLevel::Warning || level == LogLevel::Error || level == LogLevel::Fatal))
            {
                if (s_hLogFiles[0] != INVALID_HANDLE_VALUE)
                {
                    DWORD written = 0;
                    WriteFile(s_hLogFiles[0], lineBuf, (DWORD)len, &written, NULL);
                }
            }
            if (IsDebuggerPresent())
            {
                OutputDebugStringA(lineBuf);
            }
        }
    }

    void AppLogger::LogToChannelW(LogChannel channel, LogLevel level, const char* file, int line, const char* func, const wchar_t* format, ...)
    {
        if (!s_enabled) return;
        if (!s_initialized) Initialize();

        wchar_t wMsgBuf[4096];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(wMsgBuf, _countof(wMsgBuf), _TRUNCATE, format, args);
        va_end(args);

        char utf8Msg[4096 * 3];
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wMsgBuf, -1, utf8Msg, sizeof(utf8Msg), NULL, NULL);
        if (bytes <= 0) utf8Msg[0] = '\0';

        LogToChannel(channel, level, file, line, func, "%s", utf8Msg);
    }

    static const char* GetExceptionCodeString(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION (0xC0000005)";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (0xC000008C)";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT (0x80000003)";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT (0x80000002)";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND (0xC000008D)";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO (0xC000008E)";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT (0xC000008F)";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION (0xC0000090)";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW (0xC0000091)";
        case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK (0xC0000092)";
        case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW (0xC0000093)";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION (0xC000001D)";
        case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR (0xC0000006)";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO (0xC0000094)";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW (0xC0000095)";
        case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION (0xC0000026)";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION (0xC0000025)";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION (0xC0000096)";
        case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP (0x80000004)";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW (0xC00000FD)";
        default:                                 return "UNKNOWN EXCEPTION";
        }
    }

    void AppLogger::WriteCrashReport(EXCEPTION_POINTERS* pExPtrs)
    {
        if (!pExPtrs || !pExPtrs->ExceptionRecord || !pExPtrs->ContextRecord) return;

        SYSTEMTIME st;
        GetLocalTime(&st);

        std::stringstream ss;
        ss << "\r\n";
        ss << "################################################################################\r\n";
        ss << "### FATAL CRASH DETECTED AT: " << st.wYear << "-" << std::setfill('0') << std::setw(2) << st.wMonth << "-"
           << std::setw(2) << st.wDay << " " << std::setw(2) << st.wHour << ":" << std::setw(2) << st.wMinute << ":"
           << std::setw(2) << st.wSecond << "." << std::setw(3) << st.wMilliseconds << " ###\r\n";
        ss << "################################################################################\r\n";

        DWORD code = pExPtrs->ExceptionRecord->ExceptionCode;
        ss << "Exception Code    : " << GetExceptionCodeString(code) << " (0x" << std::hex << code << std::dec << ")\r\n";
        ss << "Exception Address : 0x" << std::hex << (uintptr_t)pExPtrs->ExceptionRecord->ExceptionAddress << std::dec << "\r\n";

        if (code == EXCEPTION_ACCESS_VIOLATION && pExPtrs->ExceptionRecord->NumberParameters >= 2)
        {
            ULONG_PTR opType = pExPtrs->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = pExPtrs->ExceptionRecord->ExceptionInformation[1];
            const char* opStr = (opType == 0) ? "Read from" : ((opType == 1) ? "Write to" : "Execute at");
            ss << "Access Violation  : " << opStr << " invalid address 0x" << std::hex << targetAddr << std::dec << "\r\n";
        }

        ss << "\r\n--- CALL STACK TRACE ---\r\n";

        HANDLE hProcess = GetCurrentProcess();
        HANDLE hThread = GetCurrentThread();

        CONTEXT ctx = *pExPtrs->ContextRecord;

        STACKFRAME64 frame = {};
#ifdef _M_X64
        DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
#elif _M_IX86
        DWORD machineType = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx.Eip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Esp;
        frame.AddrStack.Mode = AddrModeFlat;
#endif

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symbolBuffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        int frameNum = 0;
        while (StackWalk64(machineType, hProcess, hThread, &frame, &ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
        {
            if (frame.AddrPC.Offset == 0) break;

            DWORD64 displacement = 0;
            DWORD dispLine = 0;

            std::string symName = "UnknownFunction";
            if (SymFromAddr(hProcess, frame.AddrPC.Offset, &displacement, pSymbol))
            {
                symName = pSymbol->Name;
            }

            std::string srcFile = "UnknownSource";
            DWORD lineNum = 0;
            if (SymGetLineFromAddr64(hProcess, frame.AddrPC.Offset, &dispLine, &lineInfo))
            {
                srcFile = lineInfo.FileName;
                lineNum = lineInfo.LineNumber;
            }

            ss << "  [" << std::setw(2) << frameNum++ << "] 0x" << std::hex << frame.AddrPC.Offset << std::dec
               << " in " << symName << " (" << srcFile << ":" << lineNum << ")\r\n";
        }

        ss << "################################################################################\r\n\r\n";

        std::string report = ss.str();

        // Write to specialized CrashReport.txt in Logs folder
        std::wstring crashPath = GetLogsDirectory() + L"\\CrashReport.txt";
        HANDLE hCrashFile = CreateFileW(crashPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hCrashFile != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(hCrashFile, report.c_str(), (DWORD)report.length(), &written, NULL);
            FlushFileBuffers(hCrashFile);
            CloseHandle(hCrashFile);
        }

        // Also append to AppLog.txt
        if (s_hLogFiles[0] != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(s_hLogFiles[0], report.c_str(), (DWORD)report.length(), &written, NULL);
            FlushFileBuffers(s_hLogFiles[0]);
        }
        OutputDebugStringA(report.c_str());
    }

    LONG WINAPI AppLogger::CrashFilter(EXCEPTION_POINTERS* pExPtrs)
    {
        WriteCrashReport(pExPtrs);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}
