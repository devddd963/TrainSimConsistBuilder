#include "AppLogging.h"
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <chrono>
#include <iomanip>
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace AppLogging
{
    std::mutex AppLogger::s_logMutex;
    HANDLE AppLogger::s_hLogFile = INVALID_HANDLE_VALUE;
    std::wstring AppLogger::s_logFilePath;
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

    void AppLogger::Initialize()
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (s_initialized) return;

        // Read user's logging preference from registry
        HKEY hKey = NULL;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder\\Settings", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            DWORD dwVal = 1;
            DWORD dwSize = sizeof(dwVal);
            if (RegQueryValueExW(hKey, L"EnableAppLogging", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS)
            {
                s_enabled = (dwVal != 0);
            }
            RegCloseKey(hKey);
        }

        wchar_t szExePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        std::wstring exePath = szExePath;
        size_t lastSlash = exePath.find_last_of(L"\\/");
        std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";

        std::wstring appDataDir = dir + L"AppData";
        CreateDirectoryW(appDataDir.c_str(), NULL);

        s_logFilePath = appDataDir + L"\\AppLog.txt";

        // Open in Overwrite mode (CREATE_ALWAYS) with share read
        s_hLogFile = CreateFileW(
            s_logFilePath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (s_hLogFile != INVALID_HANDLE_VALUE)
        {
            // Write session start header
            SYSTEMTIME st;
            GetLocalTime(&st);
            char header[512];
            snprintf(header, sizeof(header),
                "================================================================================\r\n"
                "=== TRAIN SIM CONSIST BUILDER v9.0.0 SESSION LOG ===\r\n"
                "=== Started: %04d-%02d-%02d %02d:%02d:%02d.%03d | PID: %lu | Arch: x64 ===\r\n"
                "================================================================================\r\n\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentProcessId());

            DWORD written = 0;
            WriteFile(s_hLogFile, header, (DWORD)strlen(header), &written, NULL);
            FlushFileBuffers(s_hLogFile);
        }

        // Install Crash Handler
        s_prevFilter = SetUnhandledExceptionFilter(CrashFilter);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);

        s_initialized = true;
    }

    void AppLogger::SetEnabled(bool enabled)
    {
        s_enabled = enabled;
        HKEY hKey = NULL;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\TrainSimConsistBuilder\\Settings", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
        {
            DWORD dwVal = enabled ? 1 : 0;
            RegSetValueExW(hKey, L"EnableAppLogging", 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal));
            RegCloseKey(hKey);
        }

        if (enabled)
        {
            LOG_INFO("Diagnostic logging turned ON by user.");
        }
    }

    bool AppLogger::IsEnabled()
    {
        return s_enabled;
    }

    bool AppLogger::OpenLogInExplorer()
    {
        if (s_logFilePath.empty()) return false;
        HINSTANCE hInst = ShellExecuteW(NULL, L"open", s_logFilePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return ((INT_PTR)hInst > 32);
    }

    void AppLogger::Shutdown()
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (!s_initialized) return;

        if (s_prevFilter)
        {
            SetUnhandledExceptionFilter(s_prevFilter);
            s_prevFilter = nullptr;
        }

        if (s_hLogFile != INVALID_HANDLE_VALUE)
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char footer[256];
            snprintf(footer, sizeof(footer),
                "\r\n=== APPLICATION SESSION CLOSED NORMALLY: %04d-%02d-%02d %02d:%02d:%02d.%03d ===\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

            DWORD written = 0;
            WriteFile(s_hLogFile, footer, (DWORD)strlen(footer), &written, NULL);
            FlushFileBuffers(s_hLogFile);
            CloseHandle(s_hLogFile);
            s_hLogFile = INVALID_HANDLE_VALUE;
        }

        SymCleanup(GetCurrentProcess());
        s_initialized = false;
    }

    std::wstring AppLogger::GetLogFilePath()
    {
        return s_logFilePath;
    }

    void AppLogger::Flush()
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (s_hLogFile != INVALID_HANDLE_VALUE)
        {
            FlushFileBuffers(s_hLogFile);
        }
    }

    void AppLogger::Log(LogLevel level, const char* file, int line, const char* func, const char* format, ...)
    {
        if (!s_enabled) return;
        if (!s_initialized) Initialize();

        SYSTEMTIME st;
        GetLocalTime(&st);

        // Extract base filename
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
            std::lock_guard<std::mutex> lock(s_logMutex);
            if (s_hLogFile != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                WriteFile(s_hLogFile, lineBuf, (DWORD)len, &written, NULL);
                FlushFileBuffers(s_hLogFile);
            }
            OutputDebugStringA(lineBuf);
        }
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

        char utf8Msg[4096 * 3];
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wMsgBuf, -1, utf8Msg, sizeof(utf8Msg), NULL, NULL);
        if (bytes <= 0) utf8Msg[0] = '\0';

        Log(level, file, line, func, "%s", utf8Msg);
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

        // Stack Walk
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
        if (s_hLogFile != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(s_hLogFile, report.c_str(), (DWORD)report.length(), &written, NULL);
            FlushFileBuffers(s_hLogFile);
        }
        OutputDebugStringA(report.c_str());
    }

    LONG WINAPI AppLogger::CrashFilter(EXCEPTION_POINTERS* pExPtrs)
    {
        WriteCrashReport(pExPtrs);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}
