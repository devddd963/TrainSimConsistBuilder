#include "Updater.h"
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <thread>
#include "../UI/ModernMessageBox.h"
#include "../UI/UITheme.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

namespace Updater
{
    static const wchar_t VERSION_JSON_URL[] = L"https://raw.githubusercontent.com/devddd963/TrainSimConsistBuilder/main/Version.json";

    static std::wstring ToWide(const std::string& str)
    {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
        if (len <= 0) return std::wstring(str.begin(), str.end());
        std::wstring ws(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &ws[0], len);
        return ws;
    }

    static std::string FetchUrlContent(const std::wstring& url)
    {
        std::string response;
        HINTERNET hInternet = InternetOpenW(L"TrainSimConsistBuilder-Updater/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return "";

        DWORD timeout = 8000;
        InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionW(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(),
            L"Accept: application/json\r\nUser-Agent: TrainSimConsistBuilder-Updater\r\n",
            -1, flags, 0);

        if (hUrl)
        {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
            {
                response.append(buffer, bytesRead);
            }
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hInternet);
        return response;
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

    static bool ParseVersionJson(const std::string& json, UpdateInfo& outInfo)
    {
        if (json.empty()) return false;

        std::string version = ExtractJsonString(json, "version");
        int build = ExtractJsonInt(json, "build");
        std::string changelog = ExtractJsonString(json, "changelog");

#if defined(_WIN64)
        std::string downloadUrl = ExtractJsonString(json, "download_x64");
#else
        std::string downloadUrl = ExtractJsonString(json, "download_x32");
#endif

        if (version.empty() && build == 0) return false;

        outInfo.remoteVersion = ToWide(version);
        outInfo.remoteBuild = build;
        outInfo.releaseNotes = ToWide(changelog);
        outInfo.releaseTitle = L"Train Sim Consist Builder v" + outInfo.remoteVersion;

        if (!downloadUrl.empty())
        {
            outInfo.downloadUrl = ToWide(downloadUrl);
        }
        else
        {
            // Default fallback
            outInfo.downloadUrl = L"https://github.com/devddd963/TrainSimConsistBuilder/releases/download/v" +
                outInfo.remoteVersion + L"/" + APP_TARGET_EXE_NAME;
        }

        if (outInfo.remoteBuild > APP_BUILD_NUMBER)
        {
            outInfo.isUpdateAvailable = true;
        }

        return true;
    }

    void CleanupOldUpdateFiles()
    {
        wchar_t szExePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, szExePath, MAX_PATH) > 0)
        {
            std::wstring oldFile = std::wstring(szExePath) + L".old";
            std::wstring newFile = std::wstring(szExePath) + L".new";
            std::wstring tmpFile = std::wstring(szExePath) + L".tmp";
            DeleteFileW(oldFile.c_str());
            DeleteFileW(newFile.c_str());
            DeleteFileW(tmpFile.c_str());
        }
    }

    bool DownloadAndApplyUpdate(HWND hWndParent, const UpdateInfo& info)
    {
        if (info.downloadUrl.empty()) return false;

        wchar_t szCurrentExe[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, szCurrentExe, MAX_PATH) == 0) return false;

        std::wstring szNewExe = std::wstring(szCurrentExe) + L".new";
        std::wstring szOldExe = std::wstring(szCurrentExe) + L".old";

        HINTERNET hInternet = InternetOpenW(L"TrainSimConsistBuilder-Downloader/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return false;

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, info.downloadUrl.c_str(), NULL, 0, flags, 0);
        if (!hUrl)
        {
            InternetCloseHandle(hInternet);
            ShowModernMessageBox(hWndParent, L"Unable to connect to download server. Please check your internet connection.", L"Update Download Failed", MB_OK | MB_ICONERROR);
            return false;
        }

        HANDLE hFile = CreateFileW(szNewExe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            ShowModernMessageBox(hWndParent, L"Could not create temporary update file. Permission denied.", L"Update Error", MB_OK | MB_ICONERROR);
            return false;
        }

        char buffer[8192];
        DWORD bytesRead = 0;
        DWORD bytesWritten = 0;
        size_t totalBytes = 0;
        bool downloadSuccess = true;

        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
        {
            if (!WriteFile(hFile, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead)
            {
                downloadSuccess = false;
                break;
            }
            totalBytes += bytesRead;
        }

        CloseHandle(hFile);
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (!downloadSuccess || totalBytes < 100000)
        {
            DeleteFileW(szNewExe.c_str());
            ShowModernMessageBox(hWndParent, L"Downloaded file is incomplete or corrupted. Update aborted.", L"Update Failed", MB_OK | MB_ICONERROR);
            return false;
        }

        // Verify PE Header ('MZ')
        HANDLE hCheck = CreateFileW(szNewExe.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hCheck != INVALID_HANDLE_VALUE)
        {
            WORD mzHeader = 0;
            DWORD read = 0;
            ReadFile(hCheck, &mzHeader, 2, &read, NULL);
            CloseHandle(hCheck);
            if (mzHeader != 0x5A4D) // 'MZ'
            {
                DeleteFileW(szNewExe.c_str());
                ShowModernMessageBox(hWndParent, L"Downloaded file signature check failed. Update aborted.", L"Security Check", MB_OK | MB_ICONERROR);
                return false;
            }
        }

        // Atomic replacement and restart
        DeleteFileW(szOldExe.c_str());
        if (!MoveFileExW(szCurrentExe, szOldExe.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            DeleteFileW(szNewExe.c_str());
            ShowModernMessageBox(hWndParent, L"Could not prepare executable for replacement.", L"Update Error", MB_OK | MB_ICONERROR);
            return false;
        }

        if (!MoveFileExW(szNewExe.c_str(), szCurrentExe, MOVEFILE_REPLACE_EXISTING))
        {
            // Rollback
            MoveFileExW(szOldExe.c_str(), szCurrentExe, MOVEFILE_REPLACE_EXISTING);
            ShowModernMessageBox(hWndParent, L"Could not replace application executable.", L"Update Error", MB_OK | MB_ICONERROR);
            return false;
        }

        // Launch newly updated application
        ShellExecuteW(NULL, L"open", szCurrentExe, NULL, NULL, SW_SHOWNORMAL);
        ExitProcess(0);
        return true;
    }

    void CheckForUpdates(HWND hWndParent, bool silent)
    {
        std::thread([hWndParent, silent]()
        {
            std::string json = FetchUrlContent(VERSION_JSON_URL);
            UpdateInfo info;
            bool success = ParseVersionJson(json, info);

            if (!success || json.empty())
            {
                if (!silent)
                {
                    ShowModernMessageBox(hWndParent,
                        L"Unable to check for updates. Please verify your internet connection or try again later.",
                        L"Check for Updates", MB_OK | MB_ICONWARNING);
                }
                return;
            }

            if (info.isUpdateAvailable)
            {
                std::wstring msg = L"A new update is available!\n\n"
                    L"Current Version: " + std::wstring(APP_VERSION) + L" (Build " + std::to_wstring(APP_BUILD_NUMBER) + L")\n"
                    L"Latest Version: " + (info.remoteVersion.empty() ? std::wstring(APP_VERSION) : info.remoteVersion) +
                    L" (Build " + std::to_wstring(info.remoteBuild) + L")\n"
                    L"Architecture: " + std::wstring(APP_ARCH) + L"\n\n";

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
                        L"Version: " + std::wstring(APP_VERSION) + L"\n"
                        L"Build: " + std::to_wstring(APP_BUILD_NUMBER) + L"\n"
                        L"Architecture: " + std::wstring(APP_ARCH);

                    ShowModernMessageBox(hWndParent, msg.c_str(), L"Check for Updates", MB_OK | MB_ICONINFORMATION);
                }
            }
        }).detach();
    }

    void ShowAboutDialog(HWND hWndParent)
    {
        std::wstring msg = std::wstring(APP_TITLE) + L"\n\n" +
            L"Version: " + std::wstring(APP_VERSION) + L" (Build " + std::to_wstring(APP_BUILD_NUMBER) + L")\n" +
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
