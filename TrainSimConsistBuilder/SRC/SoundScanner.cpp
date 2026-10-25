#include "SoundScanner.h"
#include "AppLogging.h"
#include <algorithm>

std::vector<ScannedSoundItem> g_SoundCache;
CRITICAL_SECTION g_SoundCacheCS;
volatile BOOL g_bCancelSoundScan = FALSE;

static bool s_csInitialized = false;

struct SoundScanThreadParams {
    HWND hWndParent;
    std::wstring basePath;
};

static void ScanDirectoryForSms(
    const std::wstring& dirPath,
    const std::wstring& relBase,
    std::vector<ScannedSoundItem>& outItems,
    int depth)
{
    if (depth > 6 || g_bCancelSoundScan) return;

    std::wstring searchPattern = dirPath;
    if (!searchPattern.empty() && searchPattern.back() != L'\\') searchPattern += L'\\';
    searchPattern += L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> subDirs;

    do
    {
        if (g_bCancelSoundScan) break;
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            subDirs.push_back(ffd.cFileName);
        }
        else
        {
            size_t len = wcslen(ffd.cFileName);
            if (len >= 4 && _wcsicmp(ffd.cFileName + len - 4, L".sms") == 0)
            {
                ScannedSoundItem item;
                item.szFileName = ffd.cFileName;
                item.szFullPath = dirPath;
                if (!item.szFullPath.empty() && item.szFullPath.back() != L'\\') item.szFullPath += L'\\';
                item.szFullPath += ffd.cFileName;

                if (item.szFullPath.length() > relBase.length())
                    item.szRelativePath = item.szFullPath.substr(relBase.length());
                else
                    item.szRelativePath = item.szFileName;

                size_t lastSlash = item.szRelativePath.find_last_of(L"\\/");
                if (lastSlash != std::wstring::npos)
                    item.szFolder = item.szRelativePath.substr(0, lastSlash);
                else
                    item.szFolder = L"";

                outItems.push_back(item);
            }
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);

    for (const auto& sub : subDirs)
    {
        if (g_bCancelSoundScan) break;
        std::wstring nextDir = dirPath;
        if (!nextDir.empty() && nextDir.back() != L'\\') nextDir += L'\\';
        nextDir += sub;
        ScanDirectoryForSms(nextDir, relBase, outItems, depth + 1);
    }
}

static DWORD WINAPI SoundScanThreadProc(LPVOID lpParam)
{
    SoundScanThreadParams* pParams = (SoundScanThreadParams*)lpParam;
    HWND hWndParent = pParams->hWndParent;
    std::wstring basePath = pParams->basePath;
    delete pParams;

    if (!basePath.empty() && basePath.back() != L'\\') basePath += L'\\';

    std::vector<ScannedSoundItem> items;
    items.reserve(4096);

    // Strictly scan TRAINS\TRAINSET
    std::wstring trainsetDir = basePath + L"TRAINS\\TRAINSET\\";
    ScanDirectoryForSms(trainsetDir, trainsetDir, items, 0);

    if (g_bCancelSoundScan) return 0;

    // Sort alphabetically by filename
    std::sort(items.begin(), items.end(), [](const ScannedSoundItem& a, const ScannedSoundItem& b) {
        int cmp = _wcsicmp(a.szFileName.c_str(), b.szFileName.c_str());
        if (cmp != 0) return cmp < 0;
        return _wcsicmp(a.szFolder.c_str(), b.szFolder.c_str()) < 0;
    });

    EnterCriticalSection(&g_SoundCacheCS);
    g_SoundCache = std::move(items);
    size_t count = g_SoundCache.size();
    LeaveCriticalSection(&g_SoundCacheCS);

    LOG_INFO("Sound scan finished. Discovered %zu .sms files across library.", count);
    if (IsWindow(hWndParent))
    {
        PostMessageW(hWndParent, WM_SOUND_SCAN_COMPLETE, (WPARAM)count, 0);
    }

    return 0;
}

void InitSoundScanner()
{
    if (!s_csInitialized)
    {
        InitializeCriticalSection(&g_SoundCacheCS);
        s_csInitialized = true;
    }
}

void CleanupSoundScanner()
{
    if (s_csInitialized)
    {
        DeleteCriticalSection(&g_SoundCacheCS);
        s_csInitialized = false;
    }
}

struct SoundScannerStaticInit {
    SoundScannerStaticInit() {
        InitSoundScanner();
    }
    ~SoundScannerStaticInit() {
        CleanupSoundScanner();
    }
};
static SoundScannerStaticInit s_soundScannerAutoInit;

HANDLE StartSoundScan(HWND hWndParent, const std::wstring& basePath, bool bForceRescan)
{
    InitSoundScanner();

    EnterCriticalSection(&g_SoundCacheCS);
    if (!g_SoundCache.empty() && !bForceRescan)
    {
        size_t count = g_SoundCache.size();
        LeaveCriticalSection(&g_SoundCacheCS);
        if (IsWindow(hWndParent))
        {
            PostMessageW(hWndParent, WM_SOUND_SCAN_COMPLETE, (WPARAM)count, 0);
        }
        return NULL;
    }
    LeaveCriticalSection(&g_SoundCacheCS);

    g_bCancelSoundScan = FALSE;
    SoundScanThreadParams* pParams = new SoundScanThreadParams{ hWndParent, basePath };
    HANDLE hThread = CreateThread(NULL, 0, SoundScanThreadProc, pParams, 0, NULL);
    return hThread;
}

void CancelSoundScan(HANDLE& hThread)
{
    if (hThread)
    {
        g_bCancelSoundScan = TRUE;
        WaitForSingleObject(hThread, 3000);
        CloseHandle(hThread);
        hThread = NULL;
    }
}
