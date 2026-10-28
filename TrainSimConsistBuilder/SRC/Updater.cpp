#include "Updater.h"
#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <thread>
#include "../UI/ModernMessageBox.h"
#include "AppLogging.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace Updater
{
    static const wchar_t VERSION_JSON_PRIMARY_URL[] = L"https://raw.githubusercontent.com/devddd963/TrainSimConsistBuilder/main/Version.json";
    static const wchar_t VERSION_JSON_FALLBACK_URL[] = L"https://github.com/devddd963/TrainSimConsistBuilder/raw/main/Version.json";

    int GetCurrentBuildNumber()
    {
        return TSCB_BUILD_NUMBER;
    }

    std::wstring GetCurrentVersion()
    {
        return TSCB_VERSION_WSTR;
    }

    static std::wstring GetAppDirectory()
    {
        wchar_t szExe[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, szExe, MAX_PATH) > 0)
        {
            std::wstring path = szExe;
            size_t lastSlash = path.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos)
            {
                return path.substr(0, lastSlash + 1);
            }
        }
        return L"";
    }

    static std::wstring GetAppTempDirectory()
    {
        std::wstring appDir = GetAppDirectory();
        std::wstring appData = appDir + L"AppData";
        CreateDirectoryW(appData.c_str(), NULL);
        std::wstring tempDir = appData + L"\\Temp";
        CreateDirectoryW(tempDir.c_str(), NULL);
        // Mark AppData\Temp as HIDDEN + SYSTEM so Windows Explorer conceals it
        SetFileAttributesW(tempDir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        return tempDir;
    }

    static std::wstring ToWide(const std::string& str)
    {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
        if (len <= 0) return std::wstring(str.begin(), str.end());
        std::wstring ws(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &ws[0], len);
        return ws;
    }

    static std::string ExtractJsonString(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";

        pos += searchKey.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
        if (pos >= json.length() || json[pos] != '"') return "";

        pos++; // skip opening quote
        size_t start = pos;
        while (pos < json.length())
        {
            if (json[pos] == '\\' && pos + 1 < json.length())
            {
                pos += 2;
                continue;
            }
            if (json[pos] == '"') break;
            pos++;
        }
        return json.substr(start, pos - start);
    }

    static int ExtractJsonInt(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return 0;

        pos += searchKey.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n' || json[pos] == '"')) pos++;
        if (pos >= json.length()) return 0;

        int num = 0;
        while (pos < json.length() && isdigit((unsigned char)json[pos]))
        {
            num = num * 10 + (json[pos] - '0');
            pos++;
        }
        return num;
    }

    static std::string UnescapeJsonString(const std::string& str)
    {
        std::string result;
        result.reserve(str.length());
        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '\\' && i + 1 < str.length())
            {
                if (str[i + 1] == 'n') { result += '\n'; i++; }
                else if (str[i + 1] == 'r') { result += '\r'; i++; }
                else if (str[i + 1] == 't') { result += '\t'; i++; }
                else if (str[i + 1] == '"') { result += '"'; i++; }
                else if (str[i + 1] == '\\') { result += '\\'; i++; }
                else { result += str[i]; }
            }
            else
            {
                result += str[i];
            }
        }
        return result;
    }

    static bool DownloadFileWinHttp(const std::wstring& url, const std::wstring& destFilePath)
    {
        URL_COMPONENTSW urlComp = { sizeof(urlComp) };
        wchar_t hostName[256] = { 0 };
        wchar_t urlPath[2048] = { 0 };
        urlComp.lpszHostName = hostName;
        urlComp.dwHostNameLength = ARRAYSIZE(hostName);
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = ARRAYSIZE(urlPath);

        if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp))
        {
            LOG_ERROR("WinHttpCrackUrl failed for URL: %ls", url.c_str());
            return false;
        }

        HINTERNET hSession = WinHttpOpen(L"TrainSimConsistBuilder-Updater/2.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        // Modern timeouts: 5s connect, 5s send, 10s receive
        WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 15000);

        HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD dwFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        // Automatically follow HTTP 301/302 redirects (crucial for GitHub Releases and raw redirects)
        DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));

        // Strict cache-busting headers to prevent Fastly/proxy caching
        LPCWSTR headers = L"Cache-Control: no-cache, no-store, must-revalidate\r\nPragma: no-cache\r\nAccept: */*\r\n";
        BOOL bSend = WinHttpSendRequest(hRequest, headers, (DWORD)-1, NULL, 0, 0, 0);
        if (!bSend || !WinHttpReceiveResponse(hRequest, NULL))
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (statusCode != 200)
        {
            LOG_WARN("Download HTTP status code: %u for %ls", statusCode, url.c_str());
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        HANDLE hFile = CreateFileW(destFilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        BYTE buffer[8192];
        DWORD bytesRead = 0;
        DWORD bytesWritten = 0;
        bool success = true;

        while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
        {
            if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead)
            {
                success = false;
                break;
            }
        }

        CloseHandle(hFile);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (!success)
        {
            DeleteFileW(destFilePath.c_str());
            return false;
        }
        return true;
    }

    static bool ParseVersionJsonFile(const std::wstring& filePath, UpdateInfo& outInfo)
    {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file.is_open()) return false;

        std::stringstream ss;
        ss << file.rdbuf();
        file.close();

        std::string json = ss.str();
        if (json.empty()) return false;

        std::string version = ExtractJsonString(json, "version");
        int build = ExtractJsonInt(json, "build");
        std::string changelog = ExtractJsonString(json, "changelog");
        std::string downloadUrl = ExtractJsonString(json, "download_url");
        if (downloadUrl.empty()) downloadUrl = ExtractJsonString(json, "download");
        if (downloadUrl.empty()) downloadUrl = ExtractJsonString(json, "download_zip");

        if (version.empty() && build == 0) return false;

        outInfo.remoteVersion = ToWide(version);
        outInfo.remoteBuild = build;
        outInfo.releaseNotes = ToWide(UnescapeJsonString(changelog));
        outInfo.releaseTitle = L"Train Sim Consist Builder v" + outInfo.remoteVersion;

        if (!downloadUrl.empty())
        {
            outInfo.downloadUrl = ToWide(downloadUrl);
        }
        else
        {
            outInfo.downloadUrl = L"https://github.com/devddd963/TrainSimConsistBuilder/releases/latest/download/TrainSimConsistBuilder.zip";
        }

        // Direct, pure build integer comparison against inbuilt compile-time constant
        if (outInfo.remoteBuild > TSCB_BUILD_NUMBER)
        {
            outInfo.isUpdateAvailable = true;
        }
        else
        {
            outInfo.isUpdateAvailable = false;
        }

        return true;
    }

    void CleanupOldUpdateFiles()
    {
        std::wstring appDir = GetAppDirectory();
        std::wstring appData = appDir + L"AppData\\";

        DeleteFileW((appData + L"ApplyUpdate.cmd").c_str());

        // Remove entire Temp folder if leftover from previous sessions
        std::wstring tempDir = appData + L"Temp";
        if (PathFileExistsW(tempDir.c_str()))
        {
            std::wstring cmd = L"/c rmdir /s /q \"" + tempDir + L"\"";
            ShellExecuteW(NULL, L"open", L"cmd.exe", cmd.c_str(), NULL, SW_HIDE);
        }

        // Clean up any leftover .old / .tmp files in root directory
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((appDir + L"*.old").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do {
                DeleteFileW((appDir + fd.cFileName).c_str());
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
        hFind = FindFirstFileW((appDir + L"*.tmp").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do {
                DeleteFileW((appDir + fd.cFileName).c_str());
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    static bool ExtractZipArchive(const std::wstring& zipPath, const std::wstring& destDir)
    {
        CreateDirectoryW(destDir.c_str(), NULL);

        // 1. Try native tar.exe (included in Windows 10 build 17063+ & Windows 11)
        std::wstring tarCmd = L"/c tar.exe -xf \"" + zipPath + L"\" -C \"" + destDir + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = L"cmd.exe";
        sei.lpParameters = tarCmd.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei) && sei.hProcess)
        {
            WaitForSingleObject(sei.hProcess, 30000);
            DWORD exitCode = 1;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);
            if (exitCode == 0) return true;
        }

        // 2. Fallback to PowerShell Expand-Archive
        std::wstring psCmd = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" +
            zipPath + L"' -DestinationPath '" + destDir + L"' -Force\"";

        sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = psCmd.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei) && sei.hProcess)
        {
            WaitForSingleObject(sei.hProcess, 60000);
            DWORD exitCode = 1;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);
            return (exitCode == 0);
        }

        return false;
    }

    bool DownloadAndApplyUpdate(HWND hWndParent, const UpdateInfo& info)
    {
        if (info.downloadUrl.empty()) return false;

        std::wstring appDir = GetAppDirectory();
        std::wstring tempDir = GetAppTempDirectory();

        std::wstring szZipDownload = tempDir + L"\\TrainSimConsistBuilder_Update.zip";
        std::wstring szStagingDir = tempDir + L"\\UpdateStaging";
        std::wstring szHelperCmd = appDir + L"AppData\\ApplyUpdate.cmd";

        DeleteFileW(szZipDownload.c_str());
        DeleteFileW(szHelperCmd.c_str());

        LOG_INFO("Downloading update package from: %ls", info.downloadUrl.c_str());
        LOG_INFO("Destination zip package: %ls", szZipDownload.c_str());

        if (!DownloadFileWinHttp(info.downloadUrl, szZipDownload))
        {
            ShowModernMessageBox(hWndParent, L"Unable to download update package. Please check your internet connection.", L"Update Download Failed", MB_OK | MB_ICONERROR);
            return false;
        }

        // Verify ZIP header signature ('PK' -> 0x04034B50 or 0x06054B50)
        HANDLE hCheck = CreateFileW(szZipDownload.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hCheck != INVALID_HANDLE_VALUE)
        {
            DWORD zipHeader = 0;
            DWORD read = 0;
            ReadFile(hCheck, &zipHeader, 4, &read, NULL);
            CloseHandle(hCheck);
            if ((zipHeader & 0xFFFF) != 0x4B50) // 'PK'
            {
                DeleteFileW(szZipDownload.c_str());
                ShowModernMessageBox(hWndParent, L"Downloaded file is not a valid update archive. Update aborted.", L"Security Check", MB_OK | MB_ICONERROR);
                return false;
            }
        }

        // Extract ZIP archive into staging folder inside AppData\Temp
        LOG_INFO("Extracting update archive into staging directory: %ls", szStagingDir.c_str());
        if (!ExtractZipArchive(szZipDownload, szStagingDir))
        {
            DeleteFileW(szZipDownload.c_str());
            ShowModernMessageBox(hWndParent, L"Failed to extract update files from downloaded archive.", L"Extraction Error", MB_OK | MB_ICONERROR);
            return false;
        }

        // Determine whether current caller process is 64-bit or 32-bit
#if defined(_WIN64)
        bool isCaller64Bit = true;
#else
        bool isCaller64Bit = false;
#endif

        std::wstring targetExeName;
        std::wstring exe64, exe32;
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((szStagingDir + L"\\*.exe").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do {
                std::wstring name = fd.cFileName;
                if (name.find(L"32.exe") != std::wstring::npos || name.find(L"32") != std::wstring::npos)
                {
                    exe32 = name;
                }
                else
                {
                    exe64 = name;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        if (isCaller64Bit)
        {
            targetExeName = !exe64.empty() ? exe64 : (!exe32.empty() ? exe32 : L"TrainSimConsistBuilder.exe");
        }
        else
        {
            targetExeName = !exe32.empty() ? exe32 : (!exe64.empty() ? exe64 : L"TrainSimConsistBuilder32.exe");
        }

        // Create atomic PID-guarded update batch script:
        // 1. Loops until the specific application PID has completely exited (guarantees zero file-lock errors!)
        // 2. Deletes existing .exe and .dll files in root directory
        // 3. Copies new binaries and assets from UpdateStaging into root
        // 4. Completely deletes the entire Temp directory (rmdir /s /q "%~dp0Temp")
        // 5. Starts the new executable
        // 6. Deletes the batch script itself
        std::ofstream cmdFile(szHelperCmd, std::ios::out | std::ios::trunc);
        if (!cmdFile.is_open())
        {
            ShowModernMessageBox(hWndParent, L"Could not prepare update script.", L"Update Error", MB_OK | MB_ICONERROR);
            return false;
        }

        char szNarrowTarget[MAX_PATH] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, targetExeName.c_str(), -1, szNarrowTarget, MAX_PATH, NULL, NULL);

        cmdFile << "@echo off\r\n";
        cmdFile << "set TARGET_PID=%1\r\n";
        cmdFile << "if \"%TARGET_PID%\"==\"\" goto start_copy\r\n";
        cmdFile << ":wait_loop\r\n";
        cmdFile << "tasklist /fi \"PID eq %TARGET_PID%\" 2>nul | find \"%TARGET_PID%\" >nul\r\n";
        cmdFile << "if not errorlevel 1 (\r\n";
        cmdFile << "    timeout /t 1 /nobreak >nul\r\n";
        cmdFile << "    goto wait_loop\r\n";
        cmdFile << ")\r\n";
        cmdFile << ":start_copy\r\n";
        cmdFile << "timeout /t 1 /nobreak >nul\r\n";
        cmdFile << "del /f /q \"%~dp0..\\*.exe\" >nul 2>&1\r\n";
        cmdFile << "del /f /q \"%~dp0..\\*.dll\" >nul 2>&1\r\n";
        cmdFile << "xcopy /s /e /y /q \"%~dp0Temp\\UpdateStaging\\*\" \"%~dp0..\\\" >nul\r\n";
        cmdFile << "rmdir /s /q \"%~dp0Temp\" >nul 2>&1\r\n";
        cmdFile << "start \"\" \"%~dp0..\\" << szNarrowTarget << "\"\r\n";
        cmdFile << "(goto) 2>nul & del \"%~f0\"\r\n";
        cmdFile.close();

        DWORD curPid = GetCurrentProcessId();
        std::wstring cmdParams = std::to_wstring(curPid) + L" " + targetExeName;

        LOG_INFO("Update package ready. Target executable: %ls. Launching script '%ls' with PID %u...", targetExeName.c_str(), szHelperCmd.c_str(), curPid);

        ShellExecuteW(NULL, L"open", szHelperCmd.c_str(), cmdParams.c_str(), NULL, SW_HIDE);
        ExitProcess(0);
        return true;
    }

    void CheckForUpdates(HWND hWndParent, bool silent)
    {
        std::thread([hWndParent, silent]()
        {
            std::wstring tempDir = GetAppTempDirectory();
            std::wstring jsonPath = tempDir + L"\\Version.json";

            // 1. Delete old physical Version.json first
            DeleteFileW(jsonPath.c_str());

            // 2. Download fresh Version.json physically into AppData\Temp
            bool downloaded = DownloadFileWinHttp(VERSION_JSON_PRIMARY_URL, jsonPath);

            // Fallback to github.com raw redirect if raw.githubusercontent.com fails
            if (!downloaded)
            {
                downloaded = DownloadFileWinHttp(VERSION_JSON_FALLBACK_URL, jsonPath);
            }

            if (!downloaded || !PathFileExistsW(jsonPath.c_str()))
            {
                RemoveDirectoryW(tempDir.c_str());
                if (!silent)
                {
                    ShowModernMessageBox(hWndParent,
                        L"Unable to check for updates. Please verify your internet connection or try again later.",
                        L"Check for Updates", MB_OK | MB_ICONWARNING);
                }
                return;
            }

            // 3. Read and parse physical file
            UpdateInfo info;
            bool parsed = ParseVersionJsonFile(jsonPath, info);

            // Option A: Clean up physical Version.json and Temp folder immediately right after reading into memory
            DeleteFileW(jsonPath.c_str());
            RemoveDirectoryW(tempDir.c_str());

            if (!parsed)
            {
                if (!silent)
                {
                    ShowModernMessageBox(hWndParent,
                        L"Unable to parse update information from server.",
                        L"Check for Updates", MB_OK | MB_ICONWARNING);
                }
                return;
            }

            LOG_INFO("Update Check: Local Build %d, Remote Build %d (v%ls)",
                TSCB_BUILD_NUMBER, info.remoteBuild, info.remoteVersion.c_str());

            if (info.isUpdateAvailable)
            {
                std::wstring msg = L"A new update is available!\n\n"
                    L"Current Version: " + std::wstring(TSCB_VERSION_WSTR) + L" (Build " + std::to_wstring(TSCB_BUILD_NUMBER) + L")\n"
                    L"Latest Version: " + (info.remoteVersion.empty() ? std::wstring(TSCB_VERSION_WSTR) : info.remoteVersion) +
                    L" (Build " + std::to_wstring(info.remoteBuild) + L")\n\n";

                if (!info.releaseNotes.empty())
                {
                    msg += L"Changelog: " + info.releaseNotes + L"\n\n";
                }

                msg += L"Would you like to download and install this update now?";

                int result = ShowModernMessageBox(hWndParent, msg.c_str(), L"Update Available", MB_YESNO | MB_ICONINFORMATION);
                if (result == IDYES)
                {
                    DownloadAndApplyUpdate(hWndParent, info);
                }
            }
            else
            {
                if (!silent)
                {
                    std::wstring msg = L"You are using the latest version of " + std::wstring(APP_TITLE) + L".\n\n"
                        L"Version: " + std::wstring(TSCB_VERSION_WSTR) + L"\n"
                        L"Build: " + std::to_wstring(TSCB_BUILD_NUMBER) + L"\n"
                        L"Architecture: " + std::wstring(APP_ARCH);

                    ShowModernMessageBox(hWndParent, msg.c_str(), L"Check for Updates", MB_OK | MB_ICONINFORMATION);
                }
            }
        }).detach();
    }

    void ShowAboutDialog(HWND hWndParent)
    {
        std::wstring msg = std::wstring(APP_TITLE) + L"\n\n" +
            L"Version: " + std::wstring(TSCB_VERSION_WSTR) + L" (Build " + std::to_wstring(TSCB_BUILD_NUMBER) + L")\n" +
            L"Architecture: " + std::wstring(APP_ARCH) + L"\n\n" +
            L"A modern, high-performance visual consist editor and management suite for Open Rails.\n\n" +
            L"Would you like to check for online updates now?";

        int result = ShowModernMessageBox(hWndParent, msg.c_str(), L"About Train Sim Consist Builder", MB_YESNO | MB_ICONINFORMATION);
        if (result == IDYES)
        {
            CheckForUpdates(hWndParent, false /* non-silent */);
        }
    }
}
