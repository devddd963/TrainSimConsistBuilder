#include "Updater.h"
#include "DatabaseManager.h"
#include <windows.h>
#include <winver.h>
#include <shlwapi.h>
#include <wininet.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <thread>
#include "../UI/ModernMessageBox.h"
#include "../UI/UITheme.h"
#include "AppLogging.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")

namespace Updater
{
    static const wchar_t VERSION_JSON_URL[] = L"https://raw.githubusercontent.com/devddd963/TrainSimConsistBuilder/main/Version.json";

    static void QueryModuleVersionInfo(std::wstring& outVersion, int& outBuild)
    {
        outVersion.clear();
        outBuild = 0;

        wchar_t szExe[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, szExe, MAX_PATH) == 0) return;

        DWORD dwDummy = 0;
        DWORD dwSize = GetFileVersionInfoSizeW(szExe, &dwDummy);
        if (dwSize == 0) return;

        std::vector<BYTE> data(dwSize);
        if (!GetFileVersionInfoW(szExe, 0, dwSize, data.data())) return;

        // Try reading StringFileInfo ProductVersion
        LPWSTR lpBuffer = nullptr;
        UINT uLen = 0;
        if (VerQueryValueW(data.data(), L"\\StringFileInfo\\040904b0\\ProductVersion", (LPVOID*)&lpBuffer, &uLen) && lpBuffer && uLen > 0)
        {
            outVersion = lpBuffer;
        }

        // Try reading StringFileInfo BuildNumber
        if (VerQueryValueW(data.data(), L"\\StringFileInfo\\040904b0\\BuildNumber", (LPVOID*)&lpBuffer, &uLen) && lpBuffer && uLen > 0)
        {
            outBuild = _wtoi(lpBuffer);
        }

        // Check VS_FIXEDFILEINFO for version/build if not obtained
        VS_FIXEDFILEINFO* pFfi = nullptr;
        if (VerQueryValueW(data.data(), L"\\", (LPVOID*)&pFfi, &uLen) && pFfi)
        {
            if (outVersion.empty())
            {
                int major = HIWORD(pFfi->dwFileVersionMS);
                int minor = LOWORD(pFfi->dwFileVersionMS);
                int patch = HIWORD(pFfi->dwFileVersionLS);
                wchar_t buf[64];
                swprintf_s(buf, L"%d.%d.%d", major, minor, patch);
                outVersion = buf;
            }

            if (outBuild == 0)
            {
                int buildLS = LOWORD(pFfi->dwFileVersionLS);
                if (buildLS > 0)
                {
                    outBuild = buildLS;
                }
            }
        }
    }

    int GetCurrentBuildNumber()
    {
        std::wstring ver;
        int build = 0;
        QueryModuleVersionInfo(ver, build);
        if (build > 0) return build;
        return APP_BUILD_NUMBER;
    }

    std::wstring GetCurrentVersion()
    {
        std::wstring ver;
        int build = 0;
        QueryModuleVersionInfo(ver, build);
        if (!ver.empty()) return ver;
        return APP_VERSION;
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

    static bool IsRemoteVersionNewer(const std::wstring& remoteVer, int remoteBuild,
                                      const std::wstring& currentVer, int currentBuild)
    {
        // 1. If both build numbers are specified and valid, compare build numbers
        if (remoteBuild > 0 && currentBuild > 0)
        {
            if (remoteBuild > currentBuild) return true;
            if (remoteBuild < currentBuild) return false;
        }

        // 2. Compare semantic version components (Major.Minor.Patch)
        int rMaj = 0, rMin = 0, rPatch = 0;
        int cMaj = 0, cMin = 0, cPatch = 0;
        swscanf_s(remoteVer.c_str(), L"%d.%d.%d", &rMaj, &rMin, &rPatch);
        swscanf_s(currentVer.c_str(), L"%d.%d.%d", &cMaj, &cMin, &cPatch);

        if (rMaj != cMaj) return rMaj > cMaj;
        if (rMin != cMin) return rMin > cMin;
        if (rPatch != cPatch) return rPatch > cPatch;

        return false;
    }

    static bool ParseVersionJson(const std::string& json, UpdateInfo& outInfo)
    {
        if (json.empty()) return false;

        std::string version = ExtractJsonString(json, "version");
        int build = ExtractJsonInt(json, "build");
        std::string changelog = ExtractJsonString(json, "changelog");

        std::string downloadUrl = ExtractJsonString(json, "download_url");
        if (downloadUrl.empty()) downloadUrl = ExtractJsonString(json, "download");
        if (downloadUrl.empty()) downloadUrl = ExtractJsonString(json, "download_zip");
        if (downloadUrl.empty()) downloadUrl = ExtractJsonString(json, "download_x64");
        if (downloadUrl.empty()) downloadUrl = ExtractJsonString(json, "download_x32");

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
            // Default fallback to unified release zip
            outInfo.downloadUrl = L"https://github.com/devddd963/TrainSimConsistBuilder/releases/download/v" +
                outInfo.remoteVersion + L"/" + APP_UPDATE_ZIP_NAME;
        }

        if (IsRemoteVersionNewer(outInfo.remoteVersion, outInfo.remoteBuild, GetCurrentVersion(), GetCurrentBuildNumber()))
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
            std::wstring exePath = szExePath;
            size_t lastSlash = exePath.find_last_of(L"\\/");
            std::wstring dir = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash + 1) : L"";

            std::wstring appData = dir + L"AppData\\";
            DeleteFileW((appData + L"TrainSimConsistBuilder_Update.zip").c_str());
            DeleteFileW((appData + L"ApplyUpdate.cmd").c_str());

            // Remove leftover staging directory if exists
            std::wstring staging = appData + L"UpdateStaging";
            if (PathFileExistsW(staging.c_str()))
            {
                std::wstring cmd = L"/c rmdir /s /q \"" + staging + L"\"";
                ShellExecuteW(NULL, L"open", L"cmd.exe", cmd.c_str(), NULL, SW_HIDE);
            }

            DeleteFileW((exePath + L".old").c_str());
            DeleteFileW((exePath + L".new").c_str());
            DeleteFileW((exePath + L".tmp").c_str());

            // Also clean up any leftover .old / .tmp files in current directory
            if (!dir.empty())
            {
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW((dir + L"TrainSimConsistBuilder*.old").c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE)
                {
                    do {
                        DeleteFileW((dir + fd.cFileName).c_str());
                    } while (FindNextFileW(hFind, &fd));
                    FindClose(hFind);
                }

                hFind = FindFirstFileW((dir + L"TrainSimConsistBuilder*.tmp").c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE)
                {
                    do {
                        DeleteFileW((dir + fd.cFileName).c_str());
                    } while (FindNextFileW(hFind, &fd));
                    FindClose(hFind);
                }
            }
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

        wchar_t szCurrentExe[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, szCurrentExe, MAX_PATH) == 0) return false;

        std::wstring currentExeStr = szCurrentExe;
        std::wstring exeDir = L"";
        size_t lastSlash = currentExeStr.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            exeDir = currentExeStr.substr(0, lastSlash + 1);
        }

        std::wstring appDataDir = exeDir + L"AppData";
        CreateDirectoryW(appDataDir.c_str(), NULL);

        std::wstring szZipDownload = appDataDir + L"\\TrainSimConsistBuilder_Update.zip";
        std::wstring szStagingDir = appDataDir + L"\\UpdateStaging";
        std::wstring szHelperCmd = appDataDir + L"\\ApplyUpdate.cmd";

        DeleteFileW(szZipDownload.c_str());
        DeleteFileW(szHelperCmd.c_str());

        LOG_INFO("Downloading update package from: %ls", info.downloadUrl.c_str());
        LOG_INFO("Destination zip package: %ls", szZipDownload.c_str());

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

        HANDLE hFile = CreateFileW(szZipDownload.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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

        if (!downloadSuccess || totalBytes < 10000)
        {
            DeleteFileW(szZipDownload.c_str());
            ShowModernMessageBox(hWndParent, L"Downloaded file is incomplete or corrupted. Update aborted.", L"Update Failed", MB_OK | MB_ICONERROR);
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

        // Extract ZIP archive into staging folder
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

        // Dynamically inspect UpdateStaging directory to find target executable
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

        // Save installed build and version to database before restart
        DatabaseManager::SetSettingInt(L"InstalledBuild", info.remoteBuild);
        if (!info.remoteVersion.empty())
        {
            DatabaseManager::SetSetting(L"InstalledVersion", info.remoteVersion);
        }

        // Create atomic update batch script
        // 1. Cleans existing .exe and .dll files in root directory (safely leaves AppData, DATABASE, Screenshots, etc. intact)
        // 2. Copies new binaries and assets from UpdateStaging into root
        // 3. Cleans staging and zip
        // 4. Starts the dynamically detected target executable
        // 5. Deletes the batch script itself
        std::ofstream cmdFile(szHelperCmd, std::ios::out | std::ios::trunc);
        if (!cmdFile.is_open())
        {
            ShowModernMessageBox(hWndParent, L"Could not prepare update script.", L"Update Error", MB_OK | MB_ICONERROR);
            return false;
        }

        char szNarrowExe[MAX_PATH] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, targetExeName.c_str(), -1, szNarrowExe, MAX_PATH, NULL, NULL);

        cmdFile << "@echo off\r\n";
        cmdFile << "timeout /t 1 /nobreak >nul\r\n";
        cmdFile << "del /f /q \"%~dp0..\\*.exe\" >nul 2>&1\r\n";
        cmdFile << "del /f /q \"%~dp0..\\*.dll\" >nul 2>&1\r\n";
        cmdFile << "xcopy /s /e /y /q \"%~dp0UpdateStaging\\*\" \"%~dp0..\\\" >nul\r\n";
        cmdFile << "rmdir /s /q \"%~dp0UpdateStaging\" >nul 2>&1\r\n";
        cmdFile << "del /f /q \"%~dp0TrainSimConsistBuilder_Update.zip\" >nul 2>&1\r\n";
        cmdFile << "start \"\" \"%~dp0..\\" << szNarrowExe << "\"\r\n";
        cmdFile << "(goto) 2>nul & del \"%~f0\"\r\n";
        cmdFile.close();

        LOG_INFO("Update package ready. Target executable: %ls. Executing '%ls' and restarting...", targetExeName.c_str(), szHelperCmd.c_str());

        // Launch update script hidden and exit immediately to release DLL file locks
        ShellExecuteW(NULL, L"open", szHelperCmd.c_str(), NULL, NULL, SW_HIDE);
        ExitProcess(0);
        return true;
    }

    void CheckForUpdates(HWND hWndParent, bool silent)
    {
        std::thread([hWndParent, silent]()
        {
            // Fastly CDN cache-buster: append timestamp nonce to guarantee instant update detection
            std::wstring urlWithNonce = std::wstring(VERSION_JSON_URL) + L"?t=" + std::to_wstring(GetTickCount64());
            std::string json = FetchUrlContent(urlWithNonce);
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

            int currentBuild = GetCurrentBuildNumber();
            std::wstring currentVer = GetCurrentVersion();

            if (info.isUpdateAvailable)
            {
                std::wstring msg = L"A new update is available!\n\n"
                    L"Current Version: " + currentVer + L" (Build " + std::to_wstring(currentBuild) + L")\n"
                    L"Latest Version: " + (info.remoteVersion.empty() ? currentVer : info.remoteVersion) +
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
                        L"Version: " + currentVer + L"\n"
                        L"Build: " + std::to_wstring(currentBuild) + L"\n"
                        L"Architecture: " + std::wstring(APP_ARCH);

                    ShowModernMessageBox(hWndParent, msg.c_str(), L"Check for Updates", MB_OK | MB_ICONINFORMATION);
                }
            }
        }).detach();
    }

    void ShowAboutDialog(HWND hWndParent)
    {
        int currentBuild = GetCurrentBuildNumber();
        std::wstring currentVer = GetCurrentVersion();

        std::wstring msg = std::wstring(APP_TITLE) + L"\n\n" +
            L"Version: " + currentVer + L" (Build " + std::to_wstring(currentBuild) + L")\n" +
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
