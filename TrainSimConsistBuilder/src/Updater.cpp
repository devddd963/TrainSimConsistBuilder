#include "Updater.h"
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <thread>
#include "../ui/ModernMessageBox.h"
#include "../ui/UITheme.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

namespace Updater
{
    static const wchar_t GITHUB_API_URL[] = L"https://api.github.com/repos/devddd963/TrainSimConsistBuilder/releases/latest";

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

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(),
            L"Accept: application/vnd.github.v3+json\r\nUser-Agent: TrainSimConsistBuilder-Updater\r\n",
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

    static int ExtractBuildNumber(const std::string& text)
    {
        // Search for build pattern like "build36904", "build 36904", "build: 36904", "-36904", or any 5 digit number >= 30000
        std::string lower = text;
        for (char& c : lower) c = (char)tolower((unsigned char)c);

        size_t pos = lower.find("build");
        if (pos != std::string::npos)
        {
            size_t idx = pos + 5;
            while (idx < lower.length() && !isdigit((unsigned char)lower[idx])) idx++;
            if (idx < lower.length())
            {
                int num = 0;
                while (idx < lower.length() && isdigit((unsigned char)lower[idx]))
                {
                    num = num * 10 + (lower[idx] - '0');
                    idx++;
                }
                if (num > 0) return num;
            }
        }

        // Search for 5-digit number starting with 3
        for (size_t i = 0; i + 4 < text.length(); ++i)
        {
            if (text[i] == '3' && isdigit((unsigned char)text[i + 1]) && isdigit((unsigned char)text[i + 2]) &&
                isdigit((unsigned char)text[i + 3]) && isdigit((unsigned char)text[i + 4]))
            {
                if (i == 0 || !isdigit((unsigned char)text[i - 1]))
                {
                    if (i + 5 >= text.length() || !isdigit((unsigned char)text[i + 5]))
                    {
                        return std::stoi(text.substr(i, 5));
                    }
                }
            }
        }

        return 0;
    }

    static bool ParseReleaseJson(const std::string& json, UpdateInfo& outInfo)
    {
        if (json.empty()) return false;

        std::string tagName = ExtractJsonString(json, "tag_name");
        std::string releaseName = ExtractJsonString(json, "name");
        std::string body = ExtractJsonString(json, "body");

        outInfo.remoteVersion = ToWide(tagName);
        outInfo.releaseTitle = ToWide(releaseName);
        outInfo.releaseNotes = ToWide(body);

        // Find build number from tag, title or body
        int buildNum = ExtractBuildNumber(tagName);
        if (buildNum == 0) buildNum = ExtractBuildNumber(releaseName);
        if (buildNum == 0) buildNum = ExtractBuildNumber(body);

        outInfo.remoteBuild = buildNum;

        // Search in assets for architecture matching binary
        std::string targetPattern;
#if defined(_WIN64)
        targetPattern = "_x64.exe";
#else
        targetPattern = "_x32.exe";
#endif

        size_t posAssets = json.find("\"assets\":");
        if (posAssets != std::string::npos)
        {
            std::string assetsBlock = json.substr(posAssets);
            size_t posAsset = 0;
            while ((posAsset = assetsBlock.find("\"name\":", posAsset)) != std::string::npos)
            {
                size_t startName = assetsBlock.find('"', posAsset + 7);
                if (startName != std::string::npos)
                {
                    size_t endName = assetsBlock.find('"', startName + 1);
                    if (endName != std::string::npos)
                    {
                        std::string assetFileName = assetsBlock.substr(startName + 1, endName - startName - 1);
                        std::string assetLower = assetFileName;
                        for (char& c : assetLower) c = (char)tolower((unsigned char)c);

                        if (assetLower.find(targetPattern) != std::string::npos ||
                            (targetPattern == "_x64.exe" && assetLower.find("x64") != std::string::npos) ||
                            (targetPattern == "_x32.exe" && assetLower.find("x32") != std::string::npos))
                        {
                            // Find corresponding browser_download_url
                            size_t posUrl = assetsBlock.find("\"browser_download_url\":", posAsset);
                            if (posUrl != std::string::npos)
                            {
                                size_t startUrl = assetsBlock.find('"', posUrl + 23);
                                if (startUrl != std::string::npos)
                                {
                                    size_t endUrl = assetsBlock.find('"', startUrl + 1);
                                    if (endUrl != std::string::npos)
                                    {
                                        outInfo.downloadUrl = ToWide(assetsBlock.substr(startUrl + 1, endUrl - startUrl - 1));
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
                posAsset += 8;
            }
        }

        // If no direct asset match, fallback to releases latest html url
        if (outInfo.downloadUrl.empty())
        {
            outInfo.downloadUrl = ToWide(ExtractJsonString(json, "html_url"));
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
            std::string json = FetchUrlContent(GITHUB_API_URL);
            UpdateInfo info;
            bool success = ParseReleaseJson(json, info);

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

                if (!info.releaseTitle.empty())
                {
                    msg += L"Release: " + info.releaseTitle + L"\n\n";
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
