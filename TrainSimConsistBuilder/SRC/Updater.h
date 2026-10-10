#pragma once
#include <windows.h>
#include <string>

namespace Updater
{
    const wchar_t APP_TITLE[] = L"Train Sim Consist Builder for Open Rails";
    const wchar_t APP_VERSION[] = L"9.3.0";
    const int APP_BUILD_NUMBER = 96000;
    const wchar_t APP_UPDATE_ZIP_NAME[] = L"TrainSimConsistBuilder.zip";

#if defined(_WIN64)
    const wchar_t APP_ARCH[] = L"64-bit (x64)";
#else
    const wchar_t APP_ARCH[] = L"32-bit (x32)";
#endif

    struct UpdateInfo
    {
        bool isUpdateAvailable = false;
        int remoteBuild = 0;
        std::wstring remoteVersion;
        std::wstring releaseTitle;
        std::wstring releaseNotes;
        std::wstring downloadUrl;
        size_t assetSize = 0;
    };

    // Returns current running build number (reflects latest installed update)
    int GetCurrentBuildNumber();

    // Returns current version string
    std::wstring GetCurrentVersion();

    // Clean up any leftover .old / .tmp files from previous updates
    void CleanupOldUpdateFiles();

    // Query GitHub releases asynchronously. If silent is true, only prompts user when an update is found.
    void CheckForUpdates(HWND hWndParent, bool silent = false);

    // Download the new binary in-place, perform atomic replacement, and restart the process
    bool DownloadAndApplyUpdate(HWND hWndParent, const UpdateInfo& info);

    // Display the About & Version Info Dialog with "Check for Updates" action
    void ShowAboutDialog(HWND hWndParent);
}
