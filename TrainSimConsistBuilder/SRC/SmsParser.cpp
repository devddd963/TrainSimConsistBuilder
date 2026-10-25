#include "SmsParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static std::wstring TrimSpacesAndQuotes(const std::wstring& str)
{
    size_t first = 0;
    while (first < str.size() && (std::iswspace(str[first]) || str[first] == L'"' || str[first] == L'\''))
        ++first;
    size_t last = str.size();
    while (last > first && (std::iswspace(str[last - 1]) || str[last - 1] == L'"' || str[last - 1] == L'\''))
        --last;
    return str.substr(first, last - first);
}

std::wstring SmsParser_NormalizeRelativePath(const std::wstring& path)
{
    if (path.empty()) return L"";

    // 1. Trim surrounding spaces and quotes
    std::wstring p = TrimSpacesAndQuotes(path);
    if (p.empty()) return L"";

    // 2. Unify all backslashes to standard forward slashes (Open Rails / MSTS convention)
    for (auto& ch : p)
    {
        if (ch == L'\\') ch = L'/';
    }

    // 3. Collapse multiple consecutive slashes: // or /// -> /
    std::wstring collapsed;
    collapsed.reserve(p.size());
    for (size_t i = 0; i < p.size(); ++i)
    {
        if (p[i] == L'/' && !collapsed.empty() && collapsed.back() == L'/')
        {
            continue;
        }
        collapsed += p[i];
    }
    p = collapsed;

    // 4. Strip redundant repeated "././" sequences if present
    while (p.size() >= 4 && p[0] == L'.' && p[1] == L'/' && p[2] == L'.' && p[3] == L'/')
    {
        p.erase(0, 2);
    }

    // 5. Strip accidental root-like leading slash: e.g. "/horns/f.wav" -> "horns/f.wav"
    // (Never strip if preceded by dots, e.g. "../" or "./")
    while (!p.empty() && p[0] == L'/')
    {
        p.erase(0, 1);
    }

    return p;
}

std::wstring SmsParser_ResolveWavPath(const std::wstring& smsFilePath, const std::wstring& wavRef, const std::wstring& basePath)
{
    if (wavRef.empty()) return L"";

    // Normalize path to clean universal forward-slash representation first
    std::wstring norm = SmsParser_NormalizeRelativePath(wavRef);
    if (norm.empty()) return L"";

    // Convert normalized forward-slash path to native Windows backslashes for filesystem calls
    std::wstring nativeRef = norm;
    for (auto& ch : nativeRef)
    {
        if (ch == L'/') ch = L'\\';
    }
    // Collapse any consecutive backslashes
    std::wstring collapsedNative;
    collapsedNative.reserve(nativeRef.size());
    for (size_t i = 0; i < nativeRef.size(); ++i)
    {
        if (nativeRef[i] == L'\\' && !collapsedNative.empty() && collapsedNative.back() == L'\\')
        {
            continue;
        }
        collapsedNative += nativeRef[i];
    }
    nativeRef = collapsedNative;

    // Helper lambda to test if candidate path points to an existing file on disk
    auto FileExists = [](const std::wstring& path, std::wstring& resolvedOut) -> bool {
        if (path.empty()) return false;
        wchar_t fullBuf[MAX_PATH] = { 0 };
        if (GetFullPathNameW(path.c_str(), MAX_PATH, fullBuf, NULL))
        {
            DWORD attr = GetFileAttributesW(fullBuf);
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                resolvedOut = fullBuf;
                return true;
            }
        }
        return false;
    };

    std::wstring resolved;

    // 1. Directory of the SMS file
    size_t lastSlash = smsFilePath.find_last_of(L"\\/");
    std::wstring smsDir = (lastSlash != std::wstring::npos) ? smsFilePath.substr(0, lastSlash + 1) : L"";
    for (auto& ch : smsDir) if (ch == L'/') ch = L'\\';
    if (!smsDir.empty() && smsDir.back() != L'\\') smsDir += L'\\';

    // Anchor 1: Check relative to SMS folder (standard relative evaluation)
    if (!smsDir.empty())
    {
        if (FileExists(smsDir + nativeRef, resolved))
            return resolved;
    }

    // Determine TRAINSET directory and MSTS Root directory
    std::wstring mstsRootDir;
    std::wstring trainsetDir;

    if (!basePath.empty())
    {
        mstsRootDir = basePath;
        for (auto& ch : mstsRootDir) if (ch == L'/') ch = L'\\';
        if (mstsRootDir.back() != L'\\') mstsRootDir += L'\\';
        trainsetDir = mstsRootDir + L"TRAINS\\TRAINSET\\";
    }

    // If trainsetDir wasn't established or doesn't exist on disk, deduce from smsFilePath
    if (trainsetDir.empty() || GetFileAttributesW(trainsetDir.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::wstring smsUpper = smsFilePath;
        for (auto& c : smsUpper) c = (wchar_t)std::towupper(c);

        size_t pos = smsUpper.find(L"\\TRAINS\\TRAINSET\\");
        if (pos == std::wstring::npos) pos = smsUpper.find(L"/TRAINS/TRAINSET/");
        if (pos != std::wstring::npos)
        {
            mstsRootDir = smsFilePath.substr(0, pos + 1);
            trainsetDir = smsFilePath.substr(0, pos + 16); // includes TRAINS/TRAINSET
            for (auto& ch : mstsRootDir) if (ch == L'/') ch = L'\\';
            for (auto& ch : trainsetDir) if (ch == L'/') ch = L'\\';
        }
    }

    // Calculate cleanRelative: nativeRef with all leading '..\' and '.\' stripped
    std::wstring cleanRelative = nativeRef;
    for (auto& ch : cleanRelative)
    {
        if (ch == L'/') ch = L'\\';
    }
    while (true)
    {
        if (cleanRelative.rfind(L"..\\", 0) == 0)
        {
            cleanRelative.erase(0, 3);
        }
        else if (cleanRelative.rfind(L".\\", 0) == 0)
        {
            cleanRelative.erase(0, 2);
        }
        else if (!cleanRelative.empty() && cleanRelative[0] == L'\\')
        {
            cleanRelative.erase(0, 1);
        }
        else
        {
            break;
        }
    }

    // Anchor 2: Direct COMMON.SOUND anchor
    // If path contains common.sound, extract subpath and anchor directly at TRAINSET common.sound folder
    std::wstring nativeUpper = nativeRef;
    for (auto& c : nativeUpper) c = (wchar_t)std::towupper(c);
    size_t csPos = nativeUpper.find(L"COMMON.SOUND\\");
    if (csPos == std::wstring::npos) csPos = nativeUpper.find(L"COMMON.SOUND/");
    if (csPos != std::wstring::npos)
    {
        std::wstring subAfterCS = nativeRef.substr(csPos + 13);
        while (!subAfterCS.empty() && (subAfterCS[0] == L'\\' || subAfterCS[0] == L'/'))
            subAfterCS.erase(0, 1);

        if (!trainsetDir.empty())
        {
            // Direct under TRAINSET common.sound folder
            if (FileExists(trainsetDir + L"common.sound\\" + subAfterCS, resolved))
                return resolved;
        }

        // Anchor 3: Sibling Pack Search
        // If the SMS itself is located inside a subfolder under COMMON.SOUND (e.g. COMMON.SOUND\PackAuthor\SubFolder),
        // check inside the author/pack parent directory as well
        std::wstring smsUpper = smsFilePath;
        for (auto& c : smsUpper) c = (wchar_t)std::towupper(c);
        size_t smsCsPos = smsUpper.find(L"\\COMMON.SOUND\\");
        if (smsCsPos == std::wstring::npos) smsCsPos = smsUpper.find(L"/COMMON.SOUND/");
        if (smsCsPos != std::wstring::npos)
        {
            size_t packStart = smsCsPos + 14;
            size_t packEnd = smsUpper.find_first_of(L"\\/", packStart);
            if (packEnd != std::wstring::npos && packEnd > packStart)
            {
                std::wstring packFolder = smsFilePath.substr(packStart, packEnd - packStart);
                if (!trainsetDir.empty())
                {
                    if (FileExists(trainsetDir + L"common.sound\\" + packFolder + L"\\" + subAfterCS, resolved))
                        return resolved;
                    if (FileExists(trainsetDir + L"common.sound\\" + packFolder + L"\\" + cleanRelative, resolved))
                        return resolved;
                }
            }
        }
    }

    // Anchor 4: TRAINSET Root Resolution
    if (!trainsetDir.empty())
    {
        // 4a. Relative to TRAINSET root
        if (FileExists(trainsetDir + cleanRelative, resolved))
            return resolved;

        // 4b. Under TRAINSET common.sound folder
        if (FileExists(trainsetDir + L"common.sound\\" + cleanRelative, resolved))
            return resolved;
    }

    // Anchor 5: Global SOUND and SOUNDS Directories under MSTS Root
    if (!mstsRootDir.empty())
    {
        if (FileExists(mstsRootDir + L"SOUND\\" + cleanRelative, resolved))
            return resolved;
        if (FileExists(mstsRootDir + L"SOUNDS\\" + cleanRelative, resolved))
            return resolved;
    }

    // Anchor 6: SMS Directory with cleanRelative (in case excessive ../ were given)
    if (!smsDir.empty())
    {
        if (FileExists(smsDir + cleanRelative, resolved))
            return resolved;
    }

    // Anchor 7: TRAINSET Vehicle / Sound Pack Subfolder Scan
    // If cleanRelative contains a subfolder (e.g. "WDG_WDP\DEPHORN.wav" or "WAP5\DEPHORN.wav"),
    // search top-level folders under TRAINSET (e.g. TRAINSET\JISOO_FLOWER_903-AI_HORNS\WDG_WDP\DEPHORN.wav)
    if (!trainsetDir.empty() && cleanRelative.find(L'\\') != std::wstring::npos)
    {
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((trainsetDir + L"*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
                {
                    std::wstring candidate = trainsetDir + fd.cFileName + L"\\" + cleanRelative;
                    if (FileExists(candidate, resolved))
                    {
                        FindClose(hFind);
                        return resolved;
                    }

                    std::wstring candidateSound = trainsetDir + fd.cFileName + L"\\sound\\" + cleanRelative;
                    if (FileExists(candidateSound, resolved))
                    {
                        FindClose(hFind);
                        return resolved;
                    }
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    // Fallback: If not found on disk, produce the cleanest canonical path for UI/debugging
    wchar_t fullBuf[MAX_PATH] = { 0 };
    if (!smsDir.empty())
    {
        std::wstring candidate = smsDir + nativeRef;
        if (GetFullPathNameW(candidate.c_str(), MAX_PATH, fullBuf, NULL))
            return fullBuf;
        return candidate;
    }

    return nativeRef;
}

std::wstring SmsParser_RetargetWavRelativePath(const std::wstring& targetSmsPath, const std::wstring& resolvedWavDiskPath, const std::wstring& basePath)
{
    if (resolvedWavDiskPath.empty()) return L"";
    if (targetSmsPath.empty()) return SmsParser_NormalizeRelativePath(resolvedWavDiskPath);

    // 1. Get canonical target SMS directory
    size_t lastSlash = targetSmsPath.find_last_of(L"\\/");
    std::wstring targetDir = (lastSlash != std::wstring::npos) ? targetSmsPath.substr(0, lastSlash) : targetSmsPath;
    for (auto& ch : targetDir) if (ch == L'/') ch = L'\\';

    // 2. Canonicalize resolved WAV disk path
    std::wstring wavPath = resolvedWavDiskPath;
    for (auto& ch : wavPath) if (ch == L'/') ch = L'\\';

    // 3. Check if drive letters match
    if (targetDir.size() >= 2 && wavPath.size() >= 2)
    {
        if (targetDir[1] == L':' && wavPath[1] == L':')
        {
            if (towupper(targetDir[0]) != towupper(wavPath[0]))
            {
                // Different drives: cannot create relative path across drives.
                // Fallback to filename
                size_t wSlash = wavPath.find_last_of(L"\\/");
                return (wSlash != std::wstring::npos) ? wavPath.substr(wSlash + 1) : wavPath;
            }
        }
    }

    // 4. Use Windows PathRelativePathToW to compute exact relative path
    wchar_t relBuf[MAX_PATH] = { 0 };
    if (PathRelativePathToW(relBuf, targetDir.c_str(), FILE_ATTRIBUTE_DIRECTORY, wavPath.c_str(), FILE_ATTRIBUTE_NORMAL))
    {
        std::wstring res = SmsParser_NormalizeRelativePath(relBuf);
        // Strip any leading ./ so it stays clean (e.g. ./horns/f.wav -> horns/f.wav or ./horn.wav -> horn.wav)
        while (res.rfind(L"./", 0) == 0)
        {
            res.erase(0, 2);
        }
        return res;
    }

    // Fallback: return simple file name
    size_t wSlash = wavPath.find_last_of(L"\\/");
    return (wSlash != std::wstring::npos) ? wavPath.substr(wSlash + 1) : wavPath;
}

std::wstring SmsParser_GetDiscreteTriggerName(int triggerId)
{
    switch (triggerId)
    {
    case 1:   return L"[1] Initializer / Generic Event";
    case 2:   return L"[2] Reverser / Control Input";
    case 3:   return L"[3] Dynamic Brake Off";
    case 4:   return L"[4] Sander ON";
    case 5:   return L"[5] Sander OFF";
    case 6:   return L"[6] Wipers ON";
    case 7:   return L"[7] Wipers OFF";
    case 8:   return L"[8] Horn Start (Spacebar Down)";
    case 9:   return L"[9] Horn Release (Spacebar Up)";
    case 10:  return L"[10] Bell Start / Toggle";
    case 11:  return L"[11] Bell Release / Off";
    case 12:  return L"[12] Bell Stop";
    case 14:  return L"[14] Train Brake Application";
    case 15:  return L"[15] Train Brake Release";
    case 16:  return L"[16] Dynamic Brake Application";
    case 21:  return L"[21] Engine Brake Application";
    case 22:  return L"[22] Engine Brake Release";
    case 23:  return L"[23] Power ON (Pantograph / Diesel Start)";
    case 24:  return L"[24] Power OFF (Pantograph / Diesel Stop)";
    case 40:  return L"[40] Pantograph 2 Raise";
    case 43:  return L"[43] Pantograph 2 Lower";
    case 54:  return L"[54] Water Scoop Lower";
    case 55:  return L"[55] Water Scoop Raise";
    case 56:  return L"[56] Uncoupling Action";
    case 58:  return L"[58] Coupling Action";
    case 60:  return L"[60] Vigilance Alarm Reset";

    // Open Rails Specific Triggers
    case 90:  return L"[90] WaterPump 1 ON (ORTS Steam)";
    case 91:  return L"[91] WaterPump 1 OFF (ORTS Steam)";
    case 92:  return L"[92] WaterPump 2 ON (ORTS Steam)";
    case 93:  return L"[93] WaterPump 2 OFF (ORTS Steam)";
    case 101: return L"[101] Gear Up (ORTS Mechanical Diesel)";
    case 102: return L"[102] Gear Down (ORTS Mechanical Diesel)";
    case 105: return L"[105] Passenger Doors Open (ORTS)";
    case 106: return L"[106] Passenger Doors Close (ORTS)";
    case 109: return L"[109] TCS System Power On (ORTS)";
    case 110: return L"[110] TCS System Power Off (ORTS)";
    case 111: return L"[111] TCS Warning Sound (ORTS)";
    case 112: return L"[112] TCS Overspeed Alarm (ORTS)";
    case 113: return L"[113] TCS Emergency Brake (ORTS)";
    case 114: return L"[114] TCS Service Brake (ORTS)";
    case 115: return L"[115] TCS Acknowledged (ORTS)";
    case 116: return L"[116] TCS Mode Change (ORTS)";
    case 117: return L"[117] TCS Special Alert 1 (ORTS)";
    case 118: return L"[118] TCS Special Alert 2 (ORTS)";
    case 150: return L"[150] Circuit Breaker Closing (ORTS Electric)";
    case 151: return L"[151] Circuit Breaker Closed (ORTS Electric)";
    case 152: return L"[152] Circuit Breaker Opening (ORTS Electric)";
    case 153: return L"[153] Circuit Breaker Open (ORTS Electric)";
    case 154: return L"[154] Circuit Breaker Tripped (ORTS Electric)";
    case 155: return L"[155] Pantograph 2 Raise (ORTS)";
    case 156: return L"[156] Pantograph 2 Lower (ORTS)";
    case 157: return L"[157] Pantograph 3 Raise (ORTS)";
    case 158: return L"[158] Pantograph 3 Lower (ORTS)";
    case 159: return L"[159] Pantograph 4 Raise (ORTS)";
    case 160: return L"[160] Pantograph 4 Lower (ORTS)";
    case 161: return L"[161] Cab Light Toggle (ORTS)";
    case 162: return L"[162] Cab Radio ON (ORTS)";
    case 163: return L"[163] Cab Radio OFF (ORTS)";
    case 164: return L"[164] Steam Blowdown (ORTS)";
    case 165: return L"[165] Steam Safety Valve (ORTS)";
    case 166: return L"[166] Steam Injectors (ORTS)";
    case 169: return L"[169] Vigilance Alarm (ORTS VCD)";
    case 170: return L"[170] AWS / TPWS Sunflower (ORTS)";
    case 214: return L"[214] Traction Cut-Off Relay Open (ORTS Diesel)";
    case 215: return L"[215] Traction Cut-Off Relay Close (ORTS Diesel)";
    case 223: return L"[223] Electric Train Supply ON (ORTS HEP)";
    case 224: return L"[224] Electric Train Supply OFF (ORTS HEP)";
    case 252: return L"[252] Emergency Brake Air Venting (ORTS)";
    default:
        if (triggerId >= 121 && triggerId <= 136)
        {
            return L"[" + std::to_wstring(triggerId) + L"] Steam Chuff Sync Angle #" + std::to_wstring(triggerId - 120) + L" (ORTS)";
        }
        return L"[" + std::to_wstring(triggerId) + L"] Custom Discrete Trigger";
    }
}

// Tokenizer for SMS syntax
struct Token {
    enum Type { Word, OpenParen, CloseParen } type;
    std::wstring text;
};

static std::vector<Token> TokenizeSms(const std::wstring& src, std::vector<std::wstring>& autoRepairLog)
{
    std::vector<Token> tokens;
    size_t i = 0;
    size_t n = src.size();

    while (i < n)
    {
        wchar_t ch = src[i];
        if (std::iswspace(ch))
        {
            ++i;
            continue;
        }

        if (ch == L'(')
        {
            tokens.push_back({ Token::OpenParen, L"(" });
            ++i;
            continue;
        }
        if (ch == L')')
        {
            tokens.push_back({ Token::CloseParen, L")" });
            ++i;
            continue;
        }

        if (ch == L'"')
        {
            // Check if there is a closing quote on this line
            size_t lineEnd = i + 1;
            while (lineEnd < n && src[lineEnd] != L'\r' && src[lineEnd] != L'\n')
            {
                ++lineEnd;
            }
            size_t quoteClose = std::wstring::npos;
            for (size_t k = i + 1; k < lineEnd; ++k)
            {
                if (src[k] == L'"')
                {
                    quoteClose = k;
                    break;
                }
            }

            if (quoteClose != std::wstring::npos)
            {
                std::wstring tokenText = src.substr(i + 1, quoteClose - (i + 1));
                i = quoteClose + 1;

                // Check if immediately after this quote there is additional glued text or quotes on the same line
                // e.g. "..//..//COMMON.SOUND//ICF-V1/"ICF_10-20.wav"
                bool hadGluedText = false;
                while (i < lineEnd && !std::iswspace(src[i]) && src[i] != L'(' && src[i] != L')')
                {
                    if (src[i] == L'"')
                    {
                        hadGluedText = true;
                        ++i; // skip stray internal quote
                    }
                    else
                    {
                        size_t partStart = i;
                        while (i < lineEnd && !std::iswspace(src[i]) && src[i] != L'(' && src[i] != L')' && src[i] != L'"')
                        {
                            ++i;
                        }
                        tokenText += src.substr(partStart, i - partStart);
                        hadGluedText = true;
                    }
                }

                if (hadGluedText)
                {
                    autoRepairLog.push_back(L"Repaired path with embedded quote into '" + tokenText + L"'.");
                }

                tokens.push_back({ Token::Word, tokenText });
                continue;
            }
            else
            {
                // Stray or unclosed quote on this line.
                // Do NOT swallow parentheses into string; read only up to space or delimiter.
                ++i;
                size_t start = i;
                while (i < n && !std::iswspace(src[i]) && src[i] != L'(' && src[i] != L')' && src[i] != L'"')
                {
                    ++i;
                }
                std::wstring word = src.substr(start, i - start);
                if (!word.empty())
                {
                    tokens.push_back({ Token::Word, word });
                    autoRepairLog.push_back(L"Rescued unclosed quote around '" + word + L"'.");
                }
                continue;
            }
        }

        // Regular word token
        size_t start = i;
        while (i < n && !std::iswspace(src[i]) && src[i] != L'(' && src[i] != L')' && src[i] != L'"')
        {
            ++i;
        }
        std::wstring word = src.substr(start, i - start);

        // Check for orphan trailing quote attached directly to the word (e.g. EMD_Int1.wav")
        if (i < n && src[i] == L'"')
        {
            if (i + 1 >= n || std::iswspace(src[i + 1]) || src[i + 1] == L')' || src[i + 1] == L'\r' || src[i + 1] == L'\n')
            {
                // Consume the orphan closing quote so it does not become an opening quote for following text
                ++i;
                autoRepairLog.push_back(L"Stripped orphan trailing quote from '" + word + L"'.");
            }
        }
        tokens.push_back({ Token::Word, word });
    }

    return tokens;
}

static bool EqualsI(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

static void SkipBlock(const std::vector<Token>& tokens, size_t& idx)
{
    if (idx >= tokens.size() || tokens[idx].type != Token::OpenParen) return;
    int depth = 1;
    ++idx;
    while (idx < tokens.size() && depth > 0)
    {
        if (tokens[idx].type == Token::OpenParen) ++depth;
        else if (tokens[idx].type == Token::CloseParen) --depth;
        ++idx;
    }
}

static std::wstring ExtractSkipContent(const std::vector<Token>& tokens, size_t& idx)
{
    if (idx >= tokens.size() || tokens[idx].type != Token::OpenParen) return L"";
    ++idx;
    std::wstring result;
    int depth = 1;
    while (idx < tokens.size() && depth > 0)
    {
        if (tokens[idx].type == Token::OpenParen)
        {
            ++depth;
            result += L" ( ";
        }
        else if (tokens[idx].type == Token::CloseParen)
        {
            --depth;
            if (depth > 0) result += L" ) ";
        }
        else
        {
            if (!result.empty() && result.back() != L' ') result += L' ';
            result += tokens[idx].text;
        }
        ++idx;
    }

    // Clean up decorations like **** Title ****
    std::wstring clean = TrimSpacesAndQuotes(result);
    while (!clean.empty() && (clean.front() == L'*' || clean.front() == L' ' || clean.front() == L'-')) clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == L'*' || clean.back() == L' ' || clean.back() == L'-')) clean.pop_back();
    return TrimSpacesAndQuotes(clean);
}

static SmsCurve ParseCurve(const std::wstring& curveType, const std::vector<Token>& tokens, size_t& idx)
{
    SmsCurve curve;
    curve.curveType = curveType;

    if (idx >= tokens.size() || tokens[idx].type != Token::OpenParen) return curve;
    ++idx;

    while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
    {
        if (tokens[idx].type == Token::Word)
        {
            std::wstring kw = tokens[idx].text;
            ++idx;

            if (kw.size() >= 10 && EqualsI(kw.substr(kw.size() - 10), L"Controlled"))
            {
                curve.controlType = kw;
            }
            else if (EqualsI(kw, L"CurvePoints") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { curve.declaredPoints = std::stoi(tokens[idx].text); } catch (...) {}
                    ++idx;
                }
                while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                {
                    if (tokens[idx].type == Token::Word)
                    {
                        float xVal = 0.0f;
                        float yVal = 0.0f;
                        try { xVal = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                        if (idx < tokens.size() && tokens[idx].type == Token::Word)
                        {
                            try { yVal = std::stof(tokens[idx].text); } catch (...) {}
                            ++idx;
                            curve.points.push_back({ xVal, yVal });
                        }
                    }
                    else
                    {
                        ++idx;
                    }
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Granularity") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { curve.granularity = std::stof(tokens[idx].text); } catch (...) {}
                    ++idx;
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
        }
        else
        {
            ++idx;
        }
    }
    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
    return curve;
}

bool SmsParser_IsTrackAxleTrigger(const std::wstring& ttype)
{
    // Joint triggers: Joint_2Axle_Trigger, Joint_3Axle_Trigger, Joint_4Axle_Trigger, Joint_6Axle_Trigger, Joint_8Axle_Trigger
    if (EqualsI(ttype, L"Joint_2Axle_Trigger") || EqualsI(ttype, L"Joint_3Axle_Trigger") ||
        EqualsI(ttype, L"Joint_4Axle_Trigger") || EqualsI(ttype, L"Joint_6Axle_Trigger") ||
        EqualsI(ttype, L"Joint_8Axle_Trigger") ||
        EqualsI(ttype, L"Joint_Trigger_2") || EqualsI(ttype, L"Joint_Trigger_3") ||
        EqualsI(ttype, L"Joint_Trigger_4") || EqualsI(ttype, L"Joint_Trigger_6") ||
        EqualsI(ttype, L"Joint_Trigger_8"))
    {
        return true;
    }

    // Switch triggers: Switch_2Axle_Trigger, Switch_3Axle_Trigger, Switch_4Axle_Trigger, Switch_6Axle_Trigger, Switch_8Axle_Trigger
    if (EqualsI(ttype, L"Switch_2Axle_Trigger") || EqualsI(ttype, L"Switch_3Axle_Trigger") ||
        EqualsI(ttype, L"Switch_4Axle_Trigger") || EqualsI(ttype, L"Switch_6Axle_Trigger") ||
        EqualsI(ttype, L"Switch_8Axle_Trigger") ||
        EqualsI(ttype, L"Switch_Trigger_2") || EqualsI(ttype, L"Switch_Trigger_3") ||
        EqualsI(ttype, L"Switch_Trigger_4") || EqualsI(ttype, L"Switch_Trigger_6") ||
        EqualsI(ttype, L"Switch_Trigger_8"))
    {
        return true;
    }

    // Crossover triggers: Xover_2Axle_Trigger, Xover_3Axle_Trigger, Xover_4Axle_Trigger, Xover_6Axle_Trigger, Xover_8Axle_Trigger
    if (EqualsI(ttype, L"Xover_2Axle_Trigger") || EqualsI(ttype, L"Xover_3Axle_Trigger") ||
        EqualsI(ttype, L"Xover_4Axle_Trigger") || EqualsI(ttype, L"Xover_6Axle_Trigger") ||
        EqualsI(ttype, L"Xover_8Axle_Trigger") ||
        EqualsI(ttype, L"Xover_Trigger_2") || EqualsI(ttype, L"Xover_Trigger_3") ||
        EqualsI(ttype, L"Xover_Trigger_4") || EqualsI(ttype, L"Xover_Trigger_6") ||
        EqualsI(ttype, L"Xover_Trigger_8"))
    {
        return true;
    }

    return false;
}

bool SmsParser_IsRecognizedTriggerType(const std::wstring& ttype)
{
    if (EqualsI(ttype, L"Initial_Trigger") ||
        EqualsI(ttype, L"Discrete_Trigger") ||
        EqualsI(ttype, L"Variable_Trigger") ||
        EqualsI(ttype, L"Random_Trigger") ||
        EqualsI(ttype, L"Dist_Travelled_Trigger"))
    {
        return true;
    }

    return SmsParser_IsTrackAxleTrigger(ttype);
}

std::wstring SmsParser_FormatTriggerDisplayName(const SmsTrigger& trig)
{
    if (EqualsI(trig.triggerType, L"Discrete_Trigger"))
    {
        return SmsParser_GetDiscreteTriggerName(trig.triggerId);
    }
    else if (EqualsI(trig.triggerType, L"Variable_Trigger"))
    {
        return L"Variable: " + trig.variableCondition;
    }
    else if (EqualsI(trig.triggerType, L"Random_Trigger"))
    {
        wchar_t rBuf[64];
        if (trig.delayMin == (int)trig.delayMin && trig.delayMax == (int)trig.delayMax)
            swprintf_s(rBuf, L"Random (Delay: %d - %ds)", (int)trig.delayMin, (int)trig.delayMax);
        else
            swprintf_s(rBuf, L"Random (Delay: %.1f - %.1fs)", trig.delayMin, trig.delayMax);
        std::wstring name = rBuf;
        if (trig.hasVolumeMinMax)
        {
            wchar_t vBuf[64];
            swprintf_s(vBuf, L", Vol: %.2f - %.2f", trig.volumeMin, trig.volumeMax);
            name += vBuf;
        }
        return name;
    }
    else if (EqualsI(trig.triggerType, L"Dist_Travelled_Trigger"))
    {
        wchar_t dBuf[64];
        if (trig.distMin == (int)trig.distMin && trig.distMax == (int)trig.distMax)
            swprintf_s(dBuf, L"Dist Travelled (%d - %dm)", (int)trig.distMin, (int)trig.distMax);
        else
            swprintf_s(dBuf, L"Dist Travelled (%.1f - %.1fm)", trig.distMin, trig.distMax);
        std::wstring name = dBuf;
        if (trig.hasVolumeMinMax)
        {
            wchar_t vBuf[64];
            swprintf_s(vBuf, L", Vol: %.2f - %.2f", trig.volumeMin, trig.volumeMax);
            name += vBuf;
        }
        return name;
    }
    else if (SmsParser_IsTrackAxleTrigger(trig.triggerType))
    {
        std::wstring name = trig.triggerType;
        if (trig.hasCarCameraDist && trig.carCameraDist > 0.0f)
        {
            wchar_t distBuf[32];
            if (trig.carCameraDist == (int)trig.carCameraDist)
                swprintf_s(distBuf, L" (Cutoff: %dm)", (int)trig.carCameraDist);
            else
                swprintf_s(distBuf, L" (Cutoff: %.1fm)", trig.carCameraDist);
            name += distBuf;
        }
        return name;
    }
    else if (EqualsI(trig.triggerType, L"Initial_Trigger"))
    {
        return L"Initial Trigger (Start on Load)";
    }
    return trig.triggerType;
}

static SmsTrigger ParseTrigger(const std::wstring& trigType, const std::vector<Token>& tokens, size_t& idx, const std::wstring& smsPath, const std::wstring& basePath)
{
    SmsTrigger trig;
    trig.triggerType = trigType;

    if (idx >= tokens.size() || tokens[idx].type != Token::OpenParen) return trig;
    ++idx;

    if (EqualsI(trigType, L"Discrete_Trigger"))
    {
        if (idx < tokens.size() && tokens[idx].type == Token::Word)
        {
            try { trig.triggerId = std::stoi(tokens[idx].text); } catch (...) {}
            ++idx;
        }
    }
    else if (EqualsI(trigType, L"Variable_Trigger"))
    {
        // e.g. Speed_Inc_Past 15.0
        std::wstring cond;
        while (idx < tokens.size() && tokens[idx].type == Token::Word &&
               !EqualsI(tokens[idx].text, L"PlayOneShot") && !EqualsI(tokens[idx].text, L"StartLoop") &&
               !EqualsI(tokens[idx].text, L"StartLoopRelease") && !EqualsI(tokens[idx].text, L"ReleaseLoopRelease") &&
               !EqualsI(tokens[idx].text, L"ReleaseLoopReleaseWithJump") && !EqualsI(tokens[idx].text, L"SetStreamVolume") &&
               !EqualsI(tokens[idx].text, L"DisableTrigger") && !EqualsI(tokens[idx].text, L"EnableTrigger") &&
               !EqualsI(tokens[idx].text, L"SetVolume") && !EqualsI(tokens[idx].text, L"SetFrequency") &&
               !EqualsI(tokens[idx].text, L"SetVolumeMin") && !EqualsI(tokens[idx].text, L"SetVolumeMax"))
        {
            // Also stop if this token is immediately followed by '(' - in SMS syntax, trigger actions are followed by '('
            if (idx + 1 < tokens.size() && tokens[idx + 1].type == Token::OpenParen)
            {
                break;
            }
            if (!cond.empty()) cond += L" ";
            cond += tokens[idx].text;
            ++idx;
        }
        trig.variableCondition = cond;
    }
    else if (EqualsI(trigType, L"Random_Trigger"))
    {
        while (idx < tokens.size())
        {
            if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"Delay_Min_Max"))
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.delayMin = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.delayMax = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                }
            }
            else if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"Volume_Min_Max"))
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.volumeMin = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.volumeMax = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    trig.hasVolumeMinMax = true;
                    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                }
            }
            else
            {
                break;
            }
        }
    }
    else if (EqualsI(trigType, L"Dist_Travelled_Trigger"))
    {
        while (idx < tokens.size())
        {
            if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"Dist_Min_Max"))
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.distMin = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.distMax = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                }
            }
            else if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"Volume_Min_Max"))
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.volumeMin = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.volumeMax = std::stof(tokens[idx].text); } catch (...) {}
                        ++idx;
                    }
                    trig.hasVolumeMinMax = true;
                    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                }
            }
            else
            {
                break;
            }
        }
    }
    else if (SmsParser_IsTrackAxleTrigger(trigType))
    {
        while (idx < tokens.size())
        {
            if (tokens[idx].type == Token::Word &&
                (EqualsI(tokens[idx].text, L"Car_Camera_Dist") ||
                 EqualsI(tokens[idx].text, L"Car_Joint_Camera_Dist") ||
                 EqualsI(tokens[idx].text, L"Car_Switch_Camera_Dist") ||
                 EqualsI(tokens[idx].text, L"Car_Xover_Camera_Dist")))
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::Word)
                    {
                        try { trig.carCameraDist = std::stof(tokens[idx].text); } catch (...) {}
                        trig.hasCarCameraDist = true;
                        ++idx;
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                }
            }
            else
            {
                break;
            }
        }
    }

    // Now look for Action
    if (idx < tokens.size() && tokens[idx].type == Token::Word)
    {
        trig.actionType = tokens[idx].text;
        ++idx;

        if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
        {
            ++idx;
            // Check for file count argument if any
            bool isSoundAction = (EqualsI(trig.actionType, L"PlayOneShot") ||
                                  EqualsI(trig.actionType, L"StartLoop") ||
                                  EqualsI(trig.actionType, L"StartLoopRelease"));
            if (idx < tokens.size() && tokens[idx].type == Token::Word &&
                !EqualsI(tokens[idx].text, L"File") && !EqualsI(tokens[idx].text, L"SelectionMethod"))
            {
                try {
                    if (isSoundAction)
                    {
                        trig.declaredFiles = std::stoi(tokens[idx].text);
                    }
                    trig.actionParam = std::stof(tokens[idx].text);
                } catch (...) {}
                ++idx;
            }

            while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
            {
                if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"File"))
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                    {
                        ++idx;
                        SmsFileRef fileRef;
                        fileRef.flag = -1;

                        // Resilient token collection inside File ( ... ) until matching CloseParen
                        std::vector<std::wstring> fileWords;
                        while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                        {
                            if (tokens[idx].type == Token::Word)
                            {
                                fileWords.push_back(tokens[idx].text);
                            }
                            ++idx;
                        }
                        if (idx < tokens.size() && tokens[idx].type == Token::CloseParen)
                        {
                            ++idx; // Strictly consume matching CloseParen!
                        }

                        if (!fileWords.empty())
                        {
                            bool lastIsFlag = false;
                            if (fileWords.size() >= 2)
                            {
                                try {
                                    size_t convPos = 0;
                                    int val = std::stoi(fileWords.back(), &convPos);
                                    if (convPos == fileWords.back().size())
                                    {
                                        fileRef.flag = val;
                                        lastIsFlag = true;
                                    }
                                } catch (...) {}
                            }

                            size_t nameCount = lastIsFlag ? (fileWords.size() - 1) : fileWords.size();
                            std::wstring fullFileName;
                            for (size_t w = 0; w < nameCount; ++w)
                            {
                                fullFileName += fileWords[w];
                            }
                            fileRef.fileName = SmsParser_NormalizeRelativePath(fullFileName);
                        }

                        fileRef.resolvedDiskPath = SmsParser_ResolveWavPath(smsPath, fileRef.fileName, basePath);
                        fileRef.existsOnDisk = (!fileRef.resolvedDiskPath.empty() && GetFileAttributesW(fileRef.resolvedDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES);
                        trig.files.push_back(fileRef);
                    }
                }
                else if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"SelectionMethod"))
                {
                    ++idx;
                    if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                    {
                        ++idx;
                        if (idx < tokens.size() && tokens[idx].type == Token::Word)
                        {
                            trig.selectionMethod = tokens[idx].text;
                            ++idx;
                        }
                        if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                    }
                }
                else if (tokens[idx].type == Token::OpenParen)
                {
                    SkipBlock(tokens, idx);
                }
                else
                {
                    ++idx;
                }
            }
            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
        }
    }

    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
    return trig;
}

static SmsStream ParseStream(int streamIdx, const std::vector<Token>& tokens, size_t& idx, const std::wstring& smsPath, const std::wstring& basePath, std::vector<std::wstring>& autoRepairLog)
{
    SmsStream st;
    st.streamIndex = streamIdx;
    st.friendlyTitle = L"Stream " + std::to_wstring(streamIdx + 1);

    if (idx >= tokens.size() || tokens[idx].type != Token::OpenParen) return st;
    ++idx;

    while (idx < tokens.size())
    {
        if (tokens[idx].type == Token::CloseParen)
        {
            // Structural Auto-Correction: Check for stray closing parenthesis.
            // If the stream has no triggers yet, and the token immediately following is a stream content keyword
            // (Priority, Volume, Triggers, FrequencyCurve, VolumeCurve, Stereo), then this ')' is an accidental
            // stray bracket (e.g. from an inline comment or skip such as "Skip(...) ** )") that would prematurely kill the stream.
            if (st.triggers.empty() && idx + 1 < tokens.size() && tokens[idx + 1].type == Token::Word)
            {
                const std::wstring& nxt = tokens[idx + 1].text;
                if (EqualsI(nxt, L"Priority") || EqualsI(nxt, L"Volume") || EqualsI(nxt, L"Triggers") ||
                    EqualsI(nxt, L"FrequencyCurve") || EqualsI(nxt, L"VolumeCurve") || EqualsI(nxt, L"Stereo"))
                {
                    autoRepairLog.push_back(L"Stream #" + std::to_wstring(streamIdx + 1) + L": Ignored stray ')' before '" + nxt + L"' to prevent premature stream termination.");
                    ++idx; // Consume and discard the stray bracket!
                    continue;
                }
            }

            // Legitimate end of Stream
            ++idx;
            break;
        }

        if (tokens[idx].type == Token::Word)
        {
            std::wstring kw = tokens[idx].text;
            ++idx;

            if (EqualsI(kw, L"Skip") || EqualsI(kw, L"Comment"))
            {
                std::wstring comment = ExtractSkipContent(tokens, idx);
                if (!comment.empty() && st.friendlyTitle == (L"Stream " + std::to_wstring(streamIdx + 1)))
                {
                    st.friendlyTitle = comment;
                }
            }
            else if (EqualsI(kw, L"Priority") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { st.priority = std::stoi(tokens[idx].text); } catch (...) {}
                    ++idx;
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Volume") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { st.volume = std::stof(tokens[idx].text); st.hasVolume = true; } catch (...) {}
                    ++idx;
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Stereo") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                st.isStereo = true;
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Triggers") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { st.declaredTriggers = std::stoi(tokens[idx].text); } catch (...) {}
                    ++idx;
                }
                while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                {
                    if (tokens[idx].type == Token::Word)
                    {
                        std::wstring ttype = tokens[idx].text;
                        ++idx;
                        if (SmsParser_IsRecognizedTriggerType(ttype))
                        {
                            st.triggers.push_back(ParseTrigger(ttype, tokens, idx, smsPath, basePath));
                        }
                        else if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            SkipBlock(tokens, idx);
                        }
                    }
                    else if (tokens[idx].type == Token::OpenParen)
                    {
                        SkipBlock(tokens, idx);
                    }
                    else
                    {
                        ++idx;
                    }
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"FrequencyCurve") || EqualsI(kw, L"VolumeCurve"))
            {
                st.curves.push_back(ParseCurve(kw, tokens, idx));
            }
            else
            {
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    SkipBlock(tokens, idx);
                }
            }
        }
        else if (tokens[idx].type == Token::OpenParen)
        {
            SkipBlock(tokens, idx);
        }
        else
        {
            ++idx;
        }
    }
    return st;
}

static SmsScalabilityGroup ParseScalabilityGroup(const std::vector<Token>& tokens, size_t& idx, const std::wstring& smsPath, const std::wstring& basePath, std::vector<std::wstring>& autoRepairLog)
{
    SmsScalabilityGroup sg;
    if (idx >= tokens.size() || tokens[idx].type != Token::OpenParen) return sg;
    ++idx;

    if (idx < tokens.size() && tokens[idx].type == Token::Word)
    {
        try { sg.groupLevel = std::stoi(tokens[idx].text); } catch (...) {}
        ++idx;
    }

    while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
    {
        if (tokens[idx].type == Token::Word)
        {
            std::wstring kw = tokens[idx].text;
            ++idx;

            if (EqualsI(kw, L"Activation") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                {
                    if (tokens[idx].type == Token::Word)
                    {
                        std::wstring actKw = tokens[idx].text;
                        ++idx;
                        if (EqualsI(actKw, L"CabCam"))
                        {
                            sg.hasCabCam = true;
                            if (idx < tokens.size() && tokens[idx].type == Token::OpenParen) SkipBlock(tokens, idx);
                        }
                        else if (EqualsI(actKw, L"PassengerCam"))
                        {
                            sg.hasPassengerCam = true;
                            if (idx < tokens.size() && tokens[idx].type == Token::OpenParen) SkipBlock(tokens, idx);
                        }
                        else if (EqualsI(actKw, L"ExternalCam"))
                        {
                            sg.hasExternalCam = true;
                            if (idx < tokens.size() && tokens[idx].type == Token::OpenParen) SkipBlock(tokens, idx);
                        }
                        else if (EqualsI(actKw, L"Distance") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { sg.activationDistance = std::stof(tokens[idx].text); } catch (...) {}
                                ++idx;
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (EqualsI(actKw, L"TrackType") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { sg.trackType = std::stoi(tokens[idx].text); } catch (...) {}
                                ++idx;
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            SkipBlock(tokens, idx);
                        }
                    }
                    else if (tokens[idx].type == Token::OpenParen)
                    {
                        SkipBlock(tokens, idx);
                    }
                    else
                    {
                        ++idx;
                    }
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Deactivation") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                {
                    if (tokens[idx].type == Token::Word)
                    {
                        std::wstring deactKw = tokens[idx].text;
                        ++idx;
                        if (EqualsI(deactKw, L"Distance") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { sg.deactivationDistance = std::stof(tokens[idx].text); } catch (...) {}
                                ++idx;
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (EqualsI(deactKw, L"TrackType") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { int dt = std::stoi(tokens[idx].text); sg.deactTrackType = dt; if (sg.trackType < 0) sg.trackType = dt; } catch (...) {}
                                ++idx;
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (EqualsI(deactKw, L"CabCam"))
                        {
                            sg.deactCabCam = true;
                            if (idx < tokens.size() && tokens[idx].type == Token::OpenParen) SkipBlock(tokens, idx);
                        }
                        else if (EqualsI(deactKw, L"PassengerCam"))
                        {
                            sg.deactPassengerCam = true;
                            if (idx < tokens.size() && tokens[idx].type == Token::OpenParen) SkipBlock(tokens, idx);
                        }
                        else if (EqualsI(deactKw, L"ExternalCam"))
                        {
                            sg.deactExternalCam = true;
                            if (idx < tokens.size() && tokens[idx].type == Token::OpenParen) SkipBlock(tokens, idx);
                        }
                        else if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            SkipBlock(tokens, idx);
                        }
                    }
                    else if (tokens[idx].type == Token::OpenParen)
                    {
                        SkipBlock(tokens, idx);
                    }
                    else
                    {
                        ++idx;
                    }
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Volume") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { sg.groupVolume = std::stof(tokens[idx].text); sg.hasGroupVolume = true; } catch (...) {}
                    ++idx;
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Stereo") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                sg.isStereo = true;
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Streams") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
            {
                ++idx;
                if (idx < tokens.size() && tokens[idx].type == Token::Word)
                {
                    try { sg.declaredStreams = std::stoi(tokens[idx].text); } catch (...) {}
                    ++idx;
                }
                int curStreamIdx = 0;
                while (idx < tokens.size())
                {
                    if (tokens[idx].type == Token::CloseParen)
                    {
                        // Defensive check: If the very next token is 'Stream', this CloseParen was a stray bracket
                        // inside a previous stream or trigger, and does NOT close the Streams container!
                        if (idx + 1 < tokens.size() && tokens[idx + 1].type == Token::Word && EqualsI(tokens[idx + 1].text, L"Stream"))
                        {
                            autoRepairLog.push_back(L"Ignored stray ')' before Stream #" + std::to_wstring((int)sg.streams.size() + 1) + L" to keep it inside Streams() container.");
                            ++idx;
                            continue;
                        }
                        break;
                    }
                    if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"Stream"))
                    {
                        ++idx;
                        sg.streams.push_back(ParseStream(curStreamIdx++, tokens, idx, smsPath, basePath, autoRepairLog));
                    }
                    else if (tokens[idx].type == Token::Word && !sg.streams.empty() &&
                             (EqualsI(tokens[idx].text, L"Priority") || EqualsI(tokens[idx].text, L"Volume") ||
                              EqualsI(tokens[idx].text, L"Triggers") || EqualsI(tokens[idx].text, L"FrequencyCurve") ||
                              EqualsI(tokens[idx].text, L"VolumeCurve")))
                    {
                        // Structural Auto-Correction: Adopt orphaned blocks into preceding stream!
                        auto& prevStream = sg.streams.back();
                        std::wstring orphanedKw = tokens[idx].text;
                        ++idx;
                        if (EqualsI(orphanedKw, L"Priority") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { prevStream.priority = std::stoi(tokens[idx].text); } catch (...) {}
                                ++idx;
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (EqualsI(orphanedKw, L"Volume") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { prevStream.volume = std::stof(tokens[idx].text); prevStream.hasVolume = true; } catch (...) {}
                                ++idx;
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (EqualsI(orphanedKw, L"Triggers") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            ++idx;
                            if (idx < tokens.size() && tokens[idx].type == Token::Word)
                            {
                                try { prevStream.declaredTriggers = std::stoi(tokens[idx].text); } catch (...) {}
                                ++idx;
                            }
                            while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                            {
                                if (tokens[idx].type == Token::Word)
                                {
                                    std::wstring ttype = tokens[idx].text;
                                    ++idx;
                                    if (SmsParser_IsRecognizedTriggerType(ttype))
                                    {
                                        prevStream.triggers.push_back(ParseTrigger(ttype, tokens, idx, smsPath, basePath));
                                    }
                                    else if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                                    {
                                        SkipBlock(tokens, idx);
                                    }
                                }
                                else if (tokens[idx].type == Token::OpenParen)
                                {
                                    SkipBlock(tokens, idx);
                                }
                                else
                                {
                                    ++idx;
                                }
                            }
                            if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                        }
                        else if (EqualsI(orphanedKw, L"FrequencyCurve") || EqualsI(orphanedKw, L"VolumeCurve"))
                        {
                            prevStream.curves.push_back(ParseCurve(orphanedKw, tokens, idx));
                        }
                        autoRepairLog.push_back(L"Rescued orphaned '" + orphanedKw + L"' block and re-attached to Stream #" + std::to_wstring(prevStream.streamIndex + 1) + L".");
                    }
                    else if (tokens[idx].type == Token::OpenParen)
                    {
                        SkipBlock(tokens, idx);
                    }
                    else if (tokens[idx].type == Token::Word)
                    {
                        ++idx;
                        if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                        {
                            SkipBlock(tokens, idx);
                        }
                    }
                    else
                    {
                        ++idx;
                    }
                }
                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
            }
            else if (EqualsI(kw, L"Stream"))
            {
                // Structural Auto-Correction: Rescued misplaced stream outside Streams() container!
                sg.streams.push_back(ParseStream((int)sg.streams.size(), tokens, idx, smsPath, basePath, autoRepairLog));
                autoRepairLog.push_back(L"Rescued misplaced Stream #" + std::to_wstring(sg.streams.size()) + L" declared outside Streams() container.");
            }
            else
            {
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    SkipBlock(tokens, idx);
                }
            }
        }
        else if (tokens[idx].type == Token::OpenParen)
        {
            SkipBlock(tokens, idx);
        }
        else
        {
            ++idx;
        }
    }
    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
    return sg;
}

bool SmsParser_LoadFromFile(const std::wstring& filePath, const std::wstring& basePath, SmsDocument& outDoc)
{
    outDoc = SmsDocument();
    outDoc.filePath = filePath;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return false;
    }

    std::vector<unsigned char> buf(size);
    DWORD readBytes = 0;
    if (!ReadFile(hFile, buf.data(), size, &readBytes, NULL) || readBytes == 0)
    {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    buf.resize(readBytes);

    std::wstring text;
    // Check BOM
    if (buf.size() >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)
    {
        outDoc.encoding = SmsFileEncoding::Utf16LeBom;
        size_t count = (buf.size() - 2) / 2;
        text.assign((const wchar_t*)(buf.data() + 2), count);
    }
    else if (buf.size() >= 4 && buf[1] == 0x00 && buf[3] == 0x00)
    {
        outDoc.encoding = SmsFileEncoding::Utf16LeBom;
        size_t count = buf.size() / 2;
        text.assign((const wchar_t*)buf.data(), count);
    }
    else if (buf.size() >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
    {
        outDoc.encoding = SmsFileEncoding::Utf8Bom;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)(buf.data() + 3), (int)(buf.size() - 3), NULL, 0);
        if (wlen > 0)
        {
            text.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, (const char*)(buf.data() + 3), (int)(buf.size() - 3), &text[0], wlen);
        }
    }
    else
    {
        // Try UTF-8
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)buf.data(), (int)buf.size(), NULL, 0);
        if (wlen > 0)
        {
            outDoc.encoding = SmsFileEncoding::Utf8NoBom;
            text.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, (const char*)buf.data(), (int)buf.size(), &text[0], wlen);
        }
        else
        {
            outDoc.encoding = SmsFileEncoding::Ansi;
            wlen = MultiByteToWideChar(CP_ACP, 0, (const char*)buf.data(), (int)buf.size(), NULL, 0);
            if (wlen > 0)
            {
                text.resize(wlen);
                MultiByteToWideChar(CP_ACP, 0, (const char*)buf.data(), (int)buf.size(), &text[0], wlen);
            }
        }
    }

    std::vector<Token> tokens = TokenizeSms(text, outDoc.autoRepairLog);
    size_t idx = 0;

    while (idx < tokens.size())
    {
        if (tokens[idx].type == Token::Word)
        {
            std::wstring kw = tokens[idx].text;
            ++idx;

            if (EqualsI(kw, L"SIMISA@@@@@@@@@@JINX0x1t______"))
            {
                // Root header
            }
            else if (EqualsI(kw, L"Skip") || EqualsI(kw, L"Comment"))
            {
                std::wstring comment = ExtractSkipContent(tokens, idx);
                if (!comment.empty()) outDoc.headerComments.push_back(comment);
            }
            else if (EqualsI(kw, L"Tr_SMS"))
            {
                if (idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                {
                    ++idx;
                    while (idx < tokens.size() && tokens[idx].type != Token::CloseParen)
                    {
                        if (tokens[idx].type == Token::Word)
                        {
                            std::wstring subKw = tokens[idx].text;
                            ++idx;
                            if (EqualsI(subKw, L"Skip") || EqualsI(subKw, L"Comment"))
                            {
                                std::wstring comment = ExtractSkipContent(tokens, idx);
                                if (!comment.empty()) outDoc.headerComments.push_back(comment);
                            }
                            else if (EqualsI(subKw, L"ScalabiltyGroup") || EqualsI(subKw, L"ScalabilityGroup"))
                            {
                                outDoc.groups.push_back(ParseScalabilityGroup(tokens, idx, filePath, basePath, outDoc.autoRepairLog));
                            }
                            else if (EqualsI(subKw, L"Stream"))
                            {
                                // Structural Auto-Correction: Rescued misplaced Stream declared outside ScalabiltyGroup!
                                if (outDoc.groups.empty())
                                {
                                    SmsScalabilityGroup defaultGrp;
                                    defaultGrp.groupLevel = 5;
                                    outDoc.groups.push_back(defaultGrp);
                                }
                                auto& targetGrp = outDoc.groups.back();
                                targetGrp.streams.push_back(ParseStream((int)targetGrp.streams.size(), tokens, idx, filePath, basePath, outDoc.autoRepairLog));
                                outDoc.autoRepairLog.push_back(L"Rescued misplaced Stream #" + std::to_wstring(targetGrp.streams.size()) + L" declared outside ScalabilityGroup container.");
                            }
                            else if (EqualsI(subKw, L"Streams") && idx < tokens.size() && tokens[idx].type == Token::OpenParen)
                            {
                                // In case author declared an orphaned Streams() block outside ScalabilityGroup!
                                ++idx;
                                if (idx < tokens.size() && tokens[idx].type == Token::Word) ++idx;
                                if (outDoc.groups.empty())
                                {
                                    SmsScalabilityGroup defaultGrp;
                                    defaultGrp.groupLevel = 5;
                                    outDoc.groups.push_back(defaultGrp);
                                }
                                auto& targetGrp = outDoc.groups.back();
                                while (idx < tokens.size())
                                {
                                    if (tokens[idx].type == Token::CloseParen)
                                    {
                                        if (idx + 1 < tokens.size() && tokens[idx + 1].type == Token::Word && EqualsI(tokens[idx + 1].text, L"Stream"))
                                        {
                                            outDoc.autoRepairLog.push_back(L"Ignored stray ')' before Stream #" + std::to_wstring((int)targetGrp.streams.size() + 1) + L" to keep it inside Streams() container.");
                                            ++idx;
                                            continue;
                                        }
                                        break;
                                    }
                                    if (tokens[idx].type == Token::Word && EqualsI(tokens[idx].text, L"Stream"))
                                    {
                                        ++idx;
                                        targetGrp.streams.push_back(ParseStream((int)targetGrp.streams.size(), tokens, idx, filePath, basePath, outDoc.autoRepairLog));
                                        outDoc.autoRepairLog.push_back(L"Rescued misplaced Stream #" + std::to_wstring(targetGrp.streams.size()) + L" declared in orphaned Streams() container.");
                                    }
                                    else
                                    {
                                        ++idx;
                                    }
                                }
                                if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                            }
                            else
                            {
                                SkipBlock(tokens, idx);
                            }
                        }
                        else
                        {
                            ++idx;
                        }
                    }
                    if (idx < tokens.size() && tokens[idx].type == Token::CloseParen) ++idx;
                }
            }
            else if (EqualsI(kw, L"ScalabiltyGroup") || EqualsI(kw, L"ScalabilityGroup"))
            {
                outDoc.groups.push_back(ParseScalabilityGroup(tokens, idx, filePath, basePath, outDoc.autoRepairLog));
            }
            else if (EqualsI(kw, L"Stream"))
            {
                if (outDoc.groups.empty())
                {
                    SmsScalabilityGroup defaultGrp;
                    defaultGrp.groupLevel = 5;
                    outDoc.groups.push_back(defaultGrp);
                }
                auto& targetGrp = outDoc.groups.back();
                targetGrp.streams.push_back(ParseStream((int)targetGrp.streams.size(), tokens, idx, filePath, basePath, outDoc.autoRepairLog));
                outDoc.autoRepairLog.push_back(L"Rescued misplaced Stream #" + std::to_wstring(targetGrp.streams.size()) + L" declared outside ScalabilityGroup container.");
            }
        }
        else
        {
            ++idx;
        }
    }

    if (!outDoc.autoRepairLog.empty())
    {
        outDoc.isModified = true;
    }

    SmsParser_DetectDiscrepancies(outDoc);
    return !outDoc.groups.empty();
}

void SmsParser_DetectDiscrepancies(SmsDocument& doc)
{
    doc.syntaxWarnings.clear();

    for (size_t g = 0; g < doc.groups.size(); ++g)
    {
        const auto& grp = doc.groups[g];
        int actualStreams = (int)grp.streams.size();
        if (grp.declaredStreams != actualStreams)
        {
            std::wstring warn = L"ScalabilityGroup(" + std::to_wstring(grp.groupLevel) + L"): Declared Streams count (" +
                std::to_wstring(grp.declaredStreams) + L") does not match actual parsed streams (" + std::to_wstring(actualStreams) + L").";
            if (actualStreams > grp.declaredStreams)
            {
                warn += L" Extra streams will be ignored by Open Rails and MSTS!";
            }
            doc.syntaxWarnings.push_back(warn);
        }

        for (size_t s = 0; s < grp.streams.size(); ++s)
        {
            const auto& st = grp.streams[s];
            int actualTriggers = (int)st.triggers.size();
            if (st.declaredTriggers != actualTriggers)
            {
                doc.syntaxWarnings.push_back(
                    L"SG(" + std::to_wstring(grp.groupLevel) + L") " + st.friendlyTitle + L": Declared Triggers count (" +
                    std::to_wstring(st.declaredTriggers) + L") does not match actual parsed triggers (" + std::to_wstring(actualTriggers) + L")."
                );
            }

            for (size_t t = 0; t < st.triggers.size(); ++t)
            {
                const auto& trig = st.triggers[t];
                int actualFiles = (int)trig.files.size();
                if (trig.declaredFiles > 0 && trig.declaredFiles != actualFiles)
                {
                    doc.syntaxWarnings.push_back(
                        L"SG(" + std::to_wstring(grp.groupLevel) + L") " + st.friendlyTitle + L" Trigger " + trig.actionType +
                        L": Declared files count (" + std::to_wstring(trig.declaredFiles) + L") differs from actual (" + std::to_wstring(actualFiles) + L")."
                    );
                }
            }

            for (size_t c = 0; c < st.curves.size(); ++c)
            {
                const auto& curve = st.curves[c];
                int actualPoints = (int)curve.points.size();
                if (curve.declaredPoints > 0 && curve.declaredPoints != actualPoints)
                {
                    doc.syntaxWarnings.push_back(
                        L"SG(" + std::to_wstring(grp.groupLevel) + L") " + st.friendlyTitle + L" " + curve.curveType +
                        L": Declared curve points (" + std::to_wstring(curve.declaredPoints) + L") differs from actual (" + std::to_wstring(actualPoints) + L")."
                    );
                }
            }
        }
    }
}

void SmsParser_FixAllCounts(SmsDocument& doc)
{
    for (auto& grp : doc.groups)
    {
        grp.declaredStreams = (int)grp.streams.size();
        for (auto& st : grp.streams)
        {
            st.declaredTriggers = (int)st.triggers.size();
            for (auto& trig : st.triggers)
            {
                if (!trig.files.empty())
                {
                    trig.declaredFiles = (int)trig.files.size();
                }
            }
            for (auto& curve : st.curves)
            {
                curve.declaredPoints = (int)curve.points.size();
            }
        }
    }
    doc.isModified = true;
    SmsParser_DetectDiscrepancies(doc);
}

void SmsParser_AutoRepairAndNormalize(SmsDocument& doc)
{
    SmsParser_FixAllCounts(doc);
    for (size_t g = 0; g < doc.groups.size(); ++g)
    {
        auto& grp = doc.groups[g];
        for (size_t s = 0; s < grp.streams.size(); ++s)
        {
            auto& st = grp.streams[s];
            st.streamIndex = (int)s;
            if (st.friendlyTitle.empty() || st.friendlyTitle.rfind(L"Stream ", 0) == 0)
            {
                st.friendlyTitle = L"Stream " + std::to_wstring(s + 1);
            }
        }
    }
    doc.isModified = true;
    SmsParser_DetectDiscrepancies(doc);
}

bool SmsParser_SaveToFile(const SmsDocument& doc, const std::wstring& filePath)
{
    std::wostringstream ss;
    ss << L"SIMISA@@@@@@@@@@JINX0x1t______\r\n\r\n";
    ss << L"Tr_SMS (\r\n\r\n";

    for (const auto& c : doc.headerComments)
    {
        ss << L"\tSkip ( " << c << L" )\r\n";
    }
    if (!doc.headerComments.empty()) ss << L"\r\n";

    for (const auto& grp : doc.groups)
    {
        ss << L"\tScalabiltyGroup ( " << grp.groupLevel << L"\r\n\r\n";

        // Activation
        ss << L"\t\tActivation (\r\n";
        if (grp.hasCabCam) ss << L"\t\t\tCabCam ()\r\n";
        if (grp.hasPassengerCam) ss << L"\t\t\tPassengerCam ()\r\n";
        if (grp.hasExternalCam) ss << L"\t\t\tExternalCam ()\r\n";
        if (grp.trackType >= 0) ss << L"\t\t\tTrackType ( " << grp.trackType << L" )\r\n";
        ss << L"\t\t\tDistance ( " << (int)grp.activationDistance << L" )\r\n";
        ss << L"\t\t)\r\n";

        // Deactivation
        ss << L"\t\tDeactivation (\r\n";
        if (grp.deactCabCam) ss << L"\t\t\tCabCam ()\r\n";
        if (grp.deactPassengerCam) ss << L"\t\t\tPassengerCam ()\r\n";
        if (grp.deactExternalCam) ss << L"\t\t\tExternalCam ()\r\n";
        int dTrack = (grp.deactTrackType >= 0) ? grp.deactTrackType : (grp.trackType >= 0 ? grp.trackType : -1);
        if (dTrack >= 0) ss << L"\t\t\tTrackType ( " << dTrack << L" )\r\n";
        ss << L"\t\t\tDistance ( " << (int)grp.deactivationDistance << L" )\r\n";
        ss << L"\t\t)\r\n\r\n";

        // Global Volume
        if (grp.hasGroupVolume)
        {
            wchar_t volBuf[32];
            swprintf_s(volBuf, L"%.2f", grp.groupVolume);
            ss << L"\t\tVolume ( " << volBuf << L" )\r\n\r\n";
        }

        if (grp.isStereo)
        {
            ss << L"\t\tStereo ()\r\n\r\n";
        }

        // Streams block
        ss << L"\t\tStreams ( " << grp.declaredStreams << L"\r\n\r\n";

        for (size_t sIdx = 0; sIdx < grp.streams.size(); ++sIdx)
        {
            const auto& st = grp.streams[sIdx];
            ss << L"\t\t\tStream (\r\n";
            if (!st.friendlyTitle.empty())
            {
                ss << L"\t\t\t\tSkip ( **** " << st.friendlyTitle << L" **** )\r\n";
            }
            ss << L"\t\t\t\tPriority ( " << st.priority << L" )\r\n";
            if (st.hasVolume)
            {
                wchar_t sVol[32];
                swprintf_s(sVol, L"%.2f", st.volume);
                ss << L"\t\t\t\tVolume ( " << sVol << L" )\r\n";
            }
            if (st.isStereo)
            {
                ss << L"\t\t\t\tStereo ()\r\n";
            }

            // Triggers
            if (!st.triggers.empty())
            {
                ss << L"\r\n\t\t\t\tTriggers ( " << st.declaredTriggers << L"\r\n";
                for (size_t tIdx = 0; tIdx < st.triggers.size(); ++tIdx)
                {
                    const auto& trig = st.triggers[tIdx];
                    ss << L"\t\t\t\t\t" << trig.triggerType << L" ( ";
                    if (EqualsI(trig.triggerType, L"Discrete_Trigger"))
                    {
                        ss << trig.triggerId << L"\r\n";
                    }
                    else if (EqualsI(trig.triggerType, L"Variable_Trigger"))
                    {
                        ss << trig.variableCondition << L"\r\n";
                    }
                    else if (EqualsI(trig.triggerType, L"Random_Trigger"))
                    {
                        wchar_t dMinBuf[32], dMaxBuf[32];
                        if (trig.delayMin == (int)trig.delayMin && trig.delayMax == (int)trig.delayMax)
                        {
                            swprintf_s(dMinBuf, L"%d", (int)trig.delayMin);
                            swprintf_s(dMaxBuf, L"%d", (int)trig.delayMax);
                        }
                        else
                        {
                            swprintf_s(dMinBuf, L"%.2f", trig.delayMin);
                            swprintf_s(dMaxBuf, L"%.2f", trig.delayMax);
                        }
                        ss << L"Delay_Min_Max ( " << dMinBuf << L" " << dMaxBuf << L" )\r\n";
                        if (trig.hasVolumeMinMax)
                        {
                            wchar_t vMinBuf[32], vMaxBuf[32];
                            swprintf_s(vMinBuf, L"%.2f", trig.volumeMin);
                            swprintf_s(vMaxBuf, L"%.2f", trig.volumeMax);
                            ss << L"\t\t\t\t\t\tVolume_Min_Max ( " << vMinBuf << L" " << vMaxBuf << L" )\r\n";
                        }
                    }
                    else if (EqualsI(trig.triggerType, L"Dist_Travelled_Trigger"))
                    {
                        wchar_t dMinBuf[32], dMaxBuf[32];
                        if (trig.distMin == (int)trig.distMin && trig.distMax == (int)trig.distMax)
                        {
                            swprintf_s(dMinBuf, L"%d", (int)trig.distMin);
                            swprintf_s(dMaxBuf, L"%d", (int)trig.distMax);
                        }
                        else
                        {
                            swprintf_s(dMinBuf, L"%.2f", trig.distMin);
                            swprintf_s(dMaxBuf, L"%.2f", trig.distMax);
                        }
                        ss << L"Dist_Min_Max ( " << dMinBuf << L" " << dMaxBuf << L" )\r\n";
                        if (trig.hasVolumeMinMax)
                        {
                            wchar_t vMinBuf[32], vMaxBuf[32];
                            swprintf_s(vMinBuf, L"%.2f", trig.volumeMin);
                            swprintf_s(vMaxBuf, L"%.2f", trig.volumeMax);
                            ss << L"\t\t\t\t\t\tVolume_Min_Max ( " << vMinBuf << L" " << vMaxBuf << L" )\r\n";
                        }
                    }
                    else if (SmsParser_IsTrackAxleTrigger(trig.triggerType))
                    {
                        if (trig.hasCarCameraDist && trig.carCameraDist > 0.0f)
                        {
                            wchar_t distBuf[32];
                            if (trig.carCameraDist == (int)trig.carCameraDist)
                                swprintf_s(distBuf, L"%d", (int)trig.carCameraDist);
                            else
                                swprintf_s(distBuf, L"%.2f", trig.carCameraDist);
                            ss << L"Car_Camera_Dist ( " << distBuf << L" )\r\n";
                        }
                        else
                        {
                            ss << L"\r\n";
                        }
                    }
                    else
                    {
                        ss << L"\r\n";
                    }

                    if (trig.files.empty())
                    {
                        if (EqualsI(trig.actionType, L"DisableTrigger") || EqualsI(trig.actionType, L"EnableTrigger"))
                        {
                            if (trig.actionParam > 0.0f)
                            {
                                ss << L"\t\t\t\t\t\t" << trig.actionType << L" ( " << (int)trig.actionParam << L" )\r\n";
                            }
                            else
                            {
                                ss << L"\t\t\t\t\t\t" << trig.actionType << L" ()\r\n";
                            }
                        }
                        else if (EqualsI(trig.actionType, L"SetStreamVolume") || EqualsI(trig.actionType, L"SetVolume"))
                        {
                            wchar_t aVol[32];
                            swprintf_s(aVol, L"%.2f", trig.actionParam);
                            ss << L"\t\t\t\t\t\t" << trig.actionType << L" ( " << aVol << L" )\r\n";
                        }
                        else
                        {
                            ss << L"\t\t\t\t\t\t" << trig.actionType << L" ()\r\n";
                        }
                        ss << L"\t\t\t\t\t)\r\n";
                    }
                    else
                    {
                        ss << L"\t\t\t\t\t\t" << trig.actionType << L" ( " << (int)trig.files.size() << L"\r\n";
                        for (const auto& f : trig.files)
                        {
                            ss << L"\t\t\t\t\t\t\tFile ( \"" << SmsParser_NormalizeRelativePath(f.fileName) << L"\" " << f.flag << L" )\r\n";
                        }
                        if (!trig.selectionMethod.empty())
                        {
                            ss << L"\t\t\t\t\t\t\tSelectionMethod ( " << trig.selectionMethod << L" )\r\n";
                        }
                        ss << L"\t\t\t\t\t\t)\r\n";
                        ss << L"\t\t\t\t\t)\r\n";
                    }

                    if (tIdx + 1 < st.triggers.size())
                    {
                        ss << L"\r\n";
                    }
                }
                ss << L"\t\t\t\t)\r\n";
            }

            // Curves
            for (size_t cIdx = 0; cIdx < st.curves.size(); ++cIdx)
            {
                const auto& curve = st.curves[cIdx];
                ss << L"\r\n\t\t\t\t" << curve.curveType << L" (\r\n";
                ss << L"\t\t\t\t\t" << curve.controlType << L"\r\n";
                ss << L"\t\t\t\t\tCurvePoints ( " << curve.declaredPoints << L"\r\n";
                for (const auto& pt : curve.points)
                {
                    wchar_t ptBuf[64];
                    swprintf_s(ptBuf, L"\t\t\t\t\t\t%10.2f\t%10.4f\r\n", pt.x, pt.y);
                    ss << ptBuf;
                }
                ss << L"\t\t\t\t\t)\r\n";
                if (curve.granularity > 0.0f)
                {
                    wchar_t granBuf[32];
                    swprintf_s(granBuf, L"%.4f", curve.granularity);
                    ss << L"\t\t\t\t\tGranularity ( " << granBuf << L" )\r\n";
                }
                ss << L"\t\t\t\t)\r\n";
            }

            ss << L"\t\t\t)\r\n";
            if (sIdx + 1 < grp.streams.size())
            {
                ss << L"\r\n";
            }
        }

        ss << L"\t\t)\r\n"; // end Streams
        ss << L"\t)\r\n";   // end ScalabiltyGroup
        if (&grp != &doc.groups.back()) ss << L"\r\n";
    }

    ss << L")\r\n"; // end Tr_SMS

    std::wstring outStr = ss.str();

    // Automatic Backup: Always create .TSCB_BACKUP whenever modifying/saving an existing SMS file
    if (GetFileAttributesW(filePath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        std::wstring bakPath = filePath + L".TSCB_BACKUP";
        CopyFileW(filePath.c_str(), bakPath.c_str(), FALSE);
    }

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    if (doc.encoding == SmsFileEncoding::Utf16LeBom)
    {
        unsigned char bom[2] = { 0xFF, 0xFE };
        WriteFile(hFile, bom, 2, &bytesWritten, NULL);
        DWORD dataBytes = (DWORD)(outStr.size() * sizeof(wchar_t));
        WriteFile(hFile, outStr.data(), dataBytes, &bytesWritten, NULL);
    }
    else if (doc.encoding == SmsFileEncoding::Utf8Bom)
    {
        unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(hFile, bom, 3, &bytesWritten, NULL);
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, outStr.c_str(), (int)outStr.size(), NULL, 0, NULL, NULL);
        if (utf8Len > 0)
        {
            std::vector<char> utf8Buf(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, outStr.c_str(), (int)outStr.size(), utf8Buf.data(), utf8Len, NULL, NULL);
            WriteFile(hFile, utf8Buf.data(), utf8Len, &bytesWritten, NULL);
        }
    }
    else
    {
        // UTF-8 without BOM or ANSI
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, outStr.c_str(), (int)outStr.size(), NULL, 0, NULL, NULL);
        if (utf8Len > 0)
        {
            std::vector<char> utf8Buf(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, outStr.c_str(), (int)outStr.size(), utf8Buf.data(), utf8Len, NULL, NULL);
            WriteFile(hFile, utf8Buf.data(), utf8Len, &bytesWritten, NULL);
        }
    }

    CloseHandle(hFile);
    return true;
}

static SmsClipboard s_globalStreamClipboard;
SmsClipboard& SmsClipboard_Get()
{
    return s_globalStreamClipboard;
}
