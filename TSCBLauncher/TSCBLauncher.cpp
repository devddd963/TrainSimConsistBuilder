#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

typedef int (*FnTSCB_Run)(HINSTANCE hInstance, LPWSTR lpCmdLine, int nCmdShow);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    wchar_t szExePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    PathRemoveFileSpecW(szExePath);

    // Lock working directory and DLL search order strictly to the application's own directory
    SetCurrentDirectoryW(szExePath);
    SetDllDirectoryW(szExePath);

#ifdef _WIN64
    // 64-bit Native Launcher
    std::wstring resourceDllPath = std::wstring(szExePath) + L"\\TSCBResources64.dll";
    std::wstring coreDllPath = std::wstring(szExePath) + L"\\TSCBCore64.dll";
#else
    // 32-bit Native Launcher
    std::wstring resourceDllPath = std::wstring(szExePath) + L"\\TSCBResources32.dll";
    std::wstring coreDllPath = std::wstring(szExePath) + L"\\TSCBCore32.dll";
#endif

    // Pre-load Resource DLL (Embedded Fonts & Icons) from local folder
    LoadLibraryW(resourceDllPath.c_str());

    // Load Core Engine DLL directly in-process from local folder
    HMODULE hCore = LoadLibraryW(coreDllPath.c_str());
    if (!hCore)
    {
        DWORD err = GetLastError();
        wchar_t msg[512];
        swprintf_s(msg, 512,
            L"Train Sim Consist Builder could not start.\n\n"
            L"Failed to load core engine library:\n%ls\n\n"
            L"Error code: 0x%08X\n"
            L"Please verify that all application files and DLLs are present in this folder.",
            coreDllPath.c_str(), err);
        MessageBoxW(NULL, msg, L"Train Sim Consist Builder - Startup Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    FnTSCB_Run pfnRun = (FnTSCB_Run)GetProcAddress(hCore, "TSCB_Run");
    if (!pfnRun)
    {
        MessageBoxW(NULL,
            L"Train Sim Consist Builder could not start.\n\n"
            L"Entry point 'TSCB_Run' was not found in the core engine library.",
            L"Train Sim Consist Builder - Startup Error", MB_OK | MB_ICONERROR);
        FreeLibrary(hCore);
        return 1;
    }

    int exitCode = pfnRun(hInstance, lpCmdLine, nCmdShow);
    FreeLibrary(hCore);

    // Clean up temporary SQLite WAL and shared-memory companion files so only TSCB_DATA.db remains
    std::wstring appDataDir = std::wstring(szExePath) + L"\\AppData";
    DeleteFileW((appDataDir + L"\\TSCB_DATA.db-wal").c_str());
    DeleteFileW((appDataDir + L"\\TSCB_DATA.db-shm").c_str());

    return exitCode;
}
