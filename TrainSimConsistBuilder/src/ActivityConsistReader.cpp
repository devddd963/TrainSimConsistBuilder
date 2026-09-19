#include "ActivityConsistReader.h"
#include <windows.h>
#include <sstream>
#include <algorithm>
#include <fstream>

namespace ActivityConsistReader {

    static std::wstring Trim(const std::wstring& s) {
        const wchar_t* ws = L" \t\r\n\"";
        size_t start = s.find_first_not_of(ws);
        size_t end   = s.find_last_not_of(ws);
        return (start == std::wstring::npos) ? L"" : s.substr(start, end - start + 1);
    }

    static std::wstring ToWide(const std::string& s) {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
        if (len <= 0) return std::wstring(s.begin(), s.end());
        std::wstring ws(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &ws[0], len);
        return ws;
    }

    static std::string ReadActFileToAscii(const std::wstring& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return "";

        std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
        file.close();

        if (buffer.empty()) return "";

        // Detect UTF-16 LE with BOM
        if (buffer.size() >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE)
        {
            size_t numWChars = (buffer.size() - 2) / 2;
            const wchar_t* pWChars = (const wchar_t*)(buffer.data() + 2);
            int len = WideCharToMultiByte(CP_UTF8, 0, pWChars, (int)numWChars, NULL, 0, NULL, NULL);
            if (len <= 0) return "";
            std::string utf8(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, pWChars, (int)numWChars, &utf8[0], len, NULL, NULL);
            return utf8;
        }

        return std::string(buffer.begin(), buffer.end());
    }

    static size_t FindMatchingParen(const std::string& str, size_t openParenPos)
    {
        if (openParenPos >= str.length() || str[openParenPos] != '(') return std::string::npos;
        int depth = 1;
        bool inQuotes = false;
        for (size_t i = openParenPos + 1; i < str.length(); ++i)
        {
            char c = str[i];
            if (c == '"') inQuotes = !inQuotes;
            else if (!inQuotes)
            {
                if (c == '(') depth++;
                else if (c == ')')
                {
                    depth--;
                    if (depth == 0) return i;
                }
            }
        }
        return std::string::npos;
    }

    static void ExtractTwoTokens(const std::string& line, std::wstring& outFirst, std::wstring& outSecond)
    {
        size_t pos = line.find('(');
        if (pos == std::string::npos) return;
        ++pos;

        const size_t len = line.size();
        auto readToken = [&](std::wstring& out) -> bool
        {
            while (pos < len && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '\r' || line[pos] == '\n')) ++pos;
            if (pos >= len || line[pos] == ')') return false;

            if (line[pos] == '"')
            {
                ++pos;
                size_t start = pos;
                while (pos < len && line[pos] != '"') ++pos;
                out = ToWide(line.substr(start, pos - start));
                if (pos < len) ++pos;
            }
            else
            {
                size_t start = pos;
                while (pos < len && line[pos] != ' ' && line[pos] != '\t'
                                  && line[pos] != ')' && line[pos] != '"'
                                  && line[pos] != '\r' && line[pos] != '\n') ++pos;
                out = ToWide(line.substr(start, pos - start));
            }
            return true;
        };

        readToken(outFirst);
        readToken(outSecond);
    }

    static std::wstring ExtractTagLocal(const std::string& ascii, const std::string& tag)
    {
        std::string lower = ascii;
        for (char& c : lower) c = (char)tolower((unsigned char)c);

        size_t pos = lower.find(tag);
        while (pos != std::string::npos)
        {
            // Enforce word boundary before tag to prevent partial matches (e.g. matching "id" inside "uid")
            if (pos > 0 && isalnum((unsigned char)lower[pos - 1]))
            {
                pos = lower.find(tag, pos + 1);
                continue;
            }

            size_t idx = pos + tag.length();
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
            if (idx < lower.length() && lower[idx] == '(')
            {
                idx++;
                while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n')) idx++;
                if (idx < lower.length() && lower[idx] == '"')
                {
                    idx++;
                    size_t startVal = idx;
                    size_t endVal = ascii.find('"', startVal);
                    if (endVal != std::string::npos)
                    {
                        return ToWide(ascii.substr(startVal, endVal - startVal));
                    }
                }
                else
                {
                    size_t startVal = idx;
                    size_t endVal = lower.find_first_of(" \t\r\n)", startVal);
                    if (endVal != std::string::npos)
                    {
                        return ToWide(ascii.substr(startVal, endVal - startVal));
                    }
                }
            }
            pos = lower.find(tag, pos + 1);
        }
        return L"";
    }

    static void ParseUnitsFromBlock(const std::string& blockContent, std::vector<ConsistReader::UnitInfo>& outUnits)
    {
        std::string lower = blockContent;
        for (char& c : lower) c = (char)tolower((unsigned char)c);

        auto isTokenMatch = [&](size_t pos, const char* token) -> bool {
            size_t tLen = strlen(token);
            if (pos + tLen > lower.length()) return false;
            if (lower.compare(pos, tLen, token) != 0) return false;
            if (pos > 0 && isalnum((unsigned char)lower[pos - 1])) return false;
            size_t after = pos + tLen;
            while (after < lower.length() && (lower[after] == ' ' || lower[after] == '\t' || lower[after] == '\r' || lower[after] == '\n')) after++;
            return (after < lower.length() && lower[after] == '(');
        };

        size_t pos = 0;
        while (pos < lower.length())
        {
            bool isEng = false;
            bool isWag = false;

            if (isTokenMatch(pos, "engine"))
            {
                if (lower.compare(pos, 10, "enginedata") != 0)
                {
                    isEng = true;
                }
            }
            else if (isTokenMatch(pos, "wagon"))
            {
                if (lower.compare(pos, 9, "wagondata") != 0)
                {
                    isWag = true;
                }
            }

            if (isEng || isWag)
            {
                size_t openParen = lower.find('(', pos);
                if (openParen != std::string::npos)
                {
                    size_t closeParen = FindMatchingParen(lower, openParen);
                    if (closeParen != std::string::npos)
                    {
                        std::string unitBlock = blockContent.substr(openParen + 1, closeParen - openParen - 1);
                        std::string unitBlockLower = lower.substr(openParen + 1, closeParen - openParen - 1);

                        ConsistReader::UnitInfo u;
                        u.isEngine = isEng;
                        u.isFlipped = (unitBlockLower.find("flip (") != std::string::npos || unitBlockLower.find("flip()") != std::string::npos);

                        const char* dataKeyword = isEng ? "enginedata" : "wagondata";
                        size_t posData = unitBlockLower.find(dataKeyword);
                        if (posData != std::string::npos)
                        {
                            size_t dataOpenParen = unitBlockLower.find('(', posData);
                            if (dataOpenParen != std::string::npos)
                            {
                                size_t dataCloseParen = unitBlockLower.find(')', dataOpenParen);
                                if (dataCloseParen != std::string::npos)
                                {
                                    std::string dataLine = unitBlock.substr(posData, dataCloseParen - posData + 1);
                                    ExtractTwoTokens(dataLine, u.uid, u.parentDir);
                                }
                            }
                        }

                        if (!u.uid.empty())
                        {
                            outUnits.push_back(u);
                        }

                        pos = closeParen + 1;
                        continue;
                    }
                }
            }
            pos++;
        }
    }

    ActivityData LoadActivityConsists(const std::wstring& actFilePath, const std::wstring& basePath)
    {
        ActivityData data;
        data.filePath = actFilePath;

        // Extract route folder name from path
        size_t posRoutes = actFilePath.find(L"ROUTES\\");
        if (posRoutes == std::wstring::npos) posRoutes = actFilePath.find(L"routes\\");
        if (posRoutes != std::wstring::npos)
        {
            size_t start = posRoutes + 7;
            size_t end = actFilePath.find(L"\\", start);
            if (end != std::wstring::npos)
            {
                data.routeFolder = actFilePath.substr(start, end - start);
            }
        }

        // Extract physical .act filename
        size_t lastSlash = actFilePath.find_last_of(L"\\/");
        data.fileName = (lastSlash != std::wstring::npos) ? actFilePath.substr(lastSlash + 1) : actFilePath;

        std::string ascii = ReadActFileToAscii(actFilePath);
        if (ascii.empty()) return data;

        std::string lower = ascii;
        for (char& c : lower) c = (char)tolower((unsigned char)c);

        // Parse ActivityObjects (Embedded Loose Consists)
        size_t posObjects = lower.find("activityobjects");
        if (posObjects != std::string::npos)
        {
            size_t objOpen = lower.find('(', posObjects);
            if (objOpen != std::string::npos)
            {
                size_t objClose = FindMatchingParen(lower, objOpen);
                if (objClose != std::string::npos)
                {
                    std::string objBlock = ascii.substr(objOpen + 1, objClose - objOpen - 1);
                    std::string objLower = lower.substr(objOpen + 1, objClose - objOpen - 1);

                    size_t posObj = 0;
                    int objIndex = 0;
                    int looseIndex = 1;
                    while ((posObj = objLower.find("activityobject", posObj)) != std::string::npos)
                    {
                        size_t itemOpen = objLower.find('(', posObj);
                        if (itemOpen != std::string::npos)
                        {
                            size_t itemClose = FindMatchingParen(objLower, itemOpen);
                            if (itemClose != std::string::npos)
                            {
                                std::string itemBlock = objBlock.substr(itemOpen + 1, itemClose - itemOpen - 1);
                                std::string itemLower = objLower.substr(itemOpen + 1, itemClose - itemOpen - 1);

                                std::vector<ConsistReader::UnitInfo> looseUnits;
                                ParseUnitsFromBlock(itemBlock, looseUnits);

                                if (!looseUnits.empty())
                                {
                                    ActivityConsist looseConsist;
                                    looseConsist.objectIndex = objIndex;
                                    looseConsist.sourceTypeStr = L"Loose Consist";

                                    // Extract standalone ActivityObject ID (never confused with UiD)
                                    std::wstring objId = ExtractTagLocal(itemBlock, "id");
                                    if (objId.empty())
                                    {
                                        objId = std::to_wstring(objIndex);
                                    }

                                    // Inside Train_Config, extract TrainCfg identifier and Name
                                    std::wstring trainCfgId;
                                    size_t posCfg = itemLower.find("traincfg");
                                    if (posCfg != std::string::npos)
                                    {
                                        size_t openParen = itemLower.find('(', posCfg);
                                        if (openParen != std::string::npos)
                                        {
                                            std::wstring dummy;
                                            std::string cfgLine = itemBlock.substr(openParen, 256);
                                            ExtractTwoTokens(cfgLine, trainCfgId, dummy);
                                        }
                                    }

                                    std::wstring trainName = ExtractTagLocal(itemBlock, "name");

                                    trainCfgId = Trim(trainCfgId);
                                    trainName  = Trim(trainName);

                                    std::wstring resolvedName;
                                    if (!trainCfgId.empty()) resolvedName = trainCfgId;
                                    else if (!trainName.empty()) resolvedName = trainName;
                                    else resolvedName = L"Static Consist";

                                    looseConsist.id = objId;
                                    looseConsist.name = objId + L" : " + resolvedName;
                                    looseConsist.serviceName = L"ActivityObject " + objId;
                                    looseConsist.units = looseUnits;
                                    looseConsist.totalUnits = (int)looseUnits.size();
                                    looseConsist.trainCfg.trainCfgId = resolvedName;
                                    looseConsist.trainCfg.name = resolvedName;
                                    looseConsist.trainCfg.maxVelocity = 0.0;
                                    looseConsist.trainCfg.perfFactor = 0.3;

                                    data.consists.push_back(looseConsist);
                                    looseIndex++;
                                }

                                objIndex++;
                                posObj = itemClose + 1;
                                continue;
                            }
                        }
                        posObj += 14;
                    }
                }
            }
        }

        // Check broken status for all parsed consists
        for (auto& con : data.consists)
        {
            con.isBroken = false;
            for (const auto& u : con.units)
            {
                if (u.uid.empty() || u.parentDir.empty())
                {
                    con.isBroken = true;
                    break;
                }
                std::wstring ext = u.isEngine ? L".eng" : L".wag";
                std::wstring uPath = basePath;
                if (!uPath.empty() && uPath.back() != L'\\') uPath += L'\\';
                uPath += L"TRAINS\\TRAINSET\\" + u.parentDir + L"\\" + u.uid + ext;
                DWORD attr = GetFileAttributesW(uPath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
                {
                    con.isBroken = true;
                    break;
                }
            }
        }

        return data;
    }
}
