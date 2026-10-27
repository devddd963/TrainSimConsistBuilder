#pragma once
#include <windows.h>
#include <string>
#include <vector>

#define WM_SOUND_SCAN_COMPLETE (WM_USER + 308)
#define WM_SOUND_SCAN_PROGRESS (WM_USER + 309)

struct ScannedSoundItem {
    std::wstring szFileName;     // e.g. "ALCo251B_Cab.sms"
    std::wstring szFolder;       // Parent folder under TRAINSET (e.g. "common.sound\ALCO")
    std::wstring szRelativePath; // Relative path from TRAINSET
    std::wstring szFullPath;     // Full absolute path on disk
};

extern std::vector<ScannedSoundItem> g_SoundCache;
extern CRITICAL_SECTION g_SoundCacheCS;
extern volatile BOOL g_bCancelSoundScan;

HANDLE StartSoundScan(HWND hWndParent, const std::wstring& basePath, bool bForceRescan = false);
void CancelSoundScan(HANDLE& hThread);
void SoundScanner_RegisterOrUpdateFile(const std::wstring& fullSmsPath, const std::wstring& basePath);
void InitSoundScanner();
void CleanupSoundScanner();
