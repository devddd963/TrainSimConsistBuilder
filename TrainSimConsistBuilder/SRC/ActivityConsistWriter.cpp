#include "ActivityConsistWriter.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ActivityConsistWriter {

    static std::string ToNarrow(const std::wstring& w) {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
        if (len <= 0) return "";
        std::string s(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], len, NULL, NULL);
        return s;
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

    bool CreateBackup(const std::wstring& actFilePath)
    {
        if (actFilePath.empty()) return false;
        std::wstring bakPath = actFilePath + L".bak";
        return CopyFileW(actFilePath.c_str(), bakPath.c_str(), FALSE) != FALSE;
    }

    SaveResult SaveActivityConsist(
        const std::wstring& actFilePath,
        int targetObjectIndex,
        const std::wstring& targetObjectId,
        const std::vector<ConsistReader::UnitInfo>& updatedUnits,
        const std::wstring& consistName)
    {
        SaveResult result;
        if (actFilePath.empty())
        {
            result.errorMessage = L"Activity file path is empty.";
            return result;
        }

        // 1. Read binary content to detect encoding
        std::ifstream file(actFilePath, std::ios::binary);
        if (!file.is_open())
        {
            result.errorMessage = L"Could not open activity file for reading.";
            return result;
        }

        std::vector<unsigned char> rawBytes((std::istreambuf_iterator<char>(file)),
                                             std::istreambuf_iterator<char>());
        file.close();

        if (rawBytes.empty())
        {
            result.errorMessage = L"Activity file is empty.";
            return result;
        }

        bool isUtf16Le = false;
        std::string ascii;

        if (rawBytes.size() >= 2 && rawBytes[0] == 0xFF && rawBytes[1] == 0xFE)
        {
            isUtf16Le = true;
            // Decode UTF-16 LE to std::wstring then to UTF-8 std::string
            size_t numWChars = (rawBytes.size() - 2) / 2;
            const wchar_t* pWChars = (const wchar_t*)(rawBytes.data() + 2);
            std::wstring wText(pWChars, numWChars);
            ascii = ToNarrow(wText);
        }
        else
        {
            // ASCII / UTF-8
            ascii = std::string(rawBytes.begin(), rawBytes.end());
        }

        std::string lower = ascii;
        for (char& c : lower) c = (char)tolower((unsigned char)c);

        // 2. Locate ActivityObjects block
        size_t posObjects = lower.find("activityobjects");
        if (posObjects == std::string::npos)
        {
            result.errorMessage = L"ActivityObjects block not found in .act file.";
            return result;
        }

        size_t objOpen = lower.find('(', posObjects);
        if (objOpen == std::string::npos)
        {
            result.errorMessage = L"ActivityObjects opening parenthesis not found.";
            return result;
        }

        size_t objClose = FindMatchingParen(lower, objOpen);
        if (objClose == std::string::npos)
        {
            result.errorMessage = L"ActivityObjects closing parenthesis not found.";
            return result;
        }

        // 3. Find target ActivityObject
        size_t posObj = objOpen + 1;
        int currentObjIndex = 0;
        size_t targetObjStart = std::string::npos;
        size_t targetObjEnd = std::string::npos;

        while ((posObj = lower.find("activityobject", posObj)) != std::string::npos && posObj < objClose)
        {
            size_t itemOpen = lower.find('(', posObj);
            if (itemOpen != std::string::npos && itemOpen < objClose)
            {
                size_t itemClose = FindMatchingParen(lower, itemOpen);
                if (itemClose != std::string::npos && itemClose <= objClose)
                {
                    if (currentObjIndex == targetObjectIndex)
                    {
                        targetObjStart = posObj;
                        targetObjEnd = itemClose;
                        break;
                    }

                    currentObjIndex++;
                    posObj = itemClose + 1;
                    continue;
                }
            }
            posObj += 14;
        }

        if (targetObjStart == std::string::npos || targetObjEnd == std::string::npos)
        {
            result.errorMessage = L"Target ActivityObject index not found in .act file.";
            return result;
        }

        // 4. Locate Train_Config block inside the target ActivityObject
        std::string objSub = ascii.substr(targetObjStart, targetObjEnd - targetObjStart + 1);
        std::string objSubLower = lower.substr(targetObjStart, targetObjEnd - targetObjStart + 1);

        size_t posTrainConfig = objSubLower.find("train_config");
        size_t configStartInAscii = std::string::npos;
        size_t configEndInAscii = std::string::npos;

        if (posTrainConfig != std::string::npos)
        {
            size_t cfgOpen = objSubLower.find('(', posTrainConfig);
            if (cfgOpen != std::string::npos)
            {
                size_t cfgClose = FindMatchingParen(objSubLower, cfgOpen);
                if (cfgClose != std::string::npos)
                {
                    configStartInAscii = targetObjStart + posTrainConfig;
                    configEndInAscii = targetObjStart + cfgClose;
                }
            }
        }

        // 5. Build replacement Train_Config block with zero-indexed sequential UiDs
        auto TrimWhitespace = [](const std::wstring& s) -> std::wstring {
            if (s.empty()) return L"";
            size_t start = 0;
            while (start < s.size() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'"' || s[start] == L'\r' || s[start] == L'\n')) start++;
            size_t end = s.size();
            while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'"' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) end--;
            return s.substr(start, end - start);
        };

        std::wstring cleanName = TrimWhitespace(consistName);
        size_t colonPos = cleanName.find(L':');
        if (colonPos != std::wstring::npos)
        {
            std::wstring prefix = cleanName.substr(0, colonPos);
            prefix = TrimWhitespace(prefix);
            bool isNumeric = !prefix.empty();
            for (wchar_t ch : prefix) {
                if (!iswdigit(ch)) { isNumeric = false; break; }
            }
            if (isNumeric) {
                cleanName = TrimWhitespace(cleanName.substr(colonPos + 1));
            }
        }

        if (cleanName.empty()) {
            cleanName = L"Static Consist";
        }

        std::string narrowName = ToNarrow(cleanName);
        std::string nextWagonUidStr = std::to_string(updatedUnits.size());

        std::string newConfigBlock;
        newConfigBlock += "Train_Config (\r\n";
        newConfigBlock += "\t\t\t\tTrainCfg ( \"" + narrowName + "\"\r\n";
        newConfigBlock += "\t\t\t\t\tName ( \"" + narrowName + "\" )\r\n";
        newConfigBlock += "\t\t\t\t\tSerial ( 1 )\r\n";
        newConfigBlock += "\t\t\t\t\tMaxVelocity ( 0.00000 0.00300 )\r\n";
        newConfigBlock += "\t\t\t\t\tNextWagonUID ( " + nextWagonUidStr + " )\r\n";
        newConfigBlock += "\t\t\t\t\tDurability ( 1.00000 )\r\n";

        for (size_t i = 0; i < updatedUnits.size(); ++i)
        {
            const auto& u = updatedUnits[i];
            std::string typeTag = u.isEngine ? "Engine" : "Wagon";
            std::string dataTag = u.isEngine ? "EngineData" : "WagonData";
            std::string uidStr = std::to_string(i); // 0-indexed sequential UiD
            std::string unitName = ToNarrow(TrimWhitespace(u.uid));
            std::string folderName = ToNarrow(TrimWhitespace(u.parentDir));

            newConfigBlock += "\t\t\t\t\t" + typeTag + " (\r\n";
            newConfigBlock += "\t\t\t\t\t\t" + dataTag + " ( \"" + unitName + "\" \"" + folderName + "\" )\r\n";
            newConfigBlock += "\t\t\t\t\t\tUiD ( " + uidStr + " )\r\n";
            if (u.isFlipped)
            {
                newConfigBlock += "\t\t\t\t\t\tFlip ( )\r\n";
            }
            newConfigBlock += "\t\t\t\t\t)\r\n";
        }

        newConfigBlock += "\t\t\t\t)\r\n";
        newConfigBlock += "\t\t\t)";

        // 6. Surgically replace the Train_Config block in ascii
        std::string finalContent;
        if (configStartInAscii != std::string::npos && configEndInAscii != std::string::npos)
        {
            finalContent = ascii.substr(0, configStartInAscii) + newConfigBlock + ascii.substr(configEndInAscii + 1);
        }
        else
        {
            // Insert Train_Config right before the closing parenthesis of ActivityObject
            size_t insertPos = targetObjEnd;
            finalContent = ascii.substr(0, insertPos) + "\t\t\t" + newConfigBlock + "\r\n\t\t" + ascii.substr(insertPos);
        }

        // 7. Create safe backup before writing
        CreateBackup(actFilePath);

        // 8. Write back to disk preserving original encoding
        if (isUtf16Le)
        {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, finalContent.data(), (int)finalContent.size(), NULL, 0);
            std::wstring wOut(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, finalContent.data(), (int)finalContent.size(), &wOut[0], wlen);

            std::ofstream outFile(actFilePath, std::ios::binary | std::ios::trunc);
            if (!outFile.is_open())
            {
                result.errorMessage = L"Could not open activity file for writing.";
                return result;
            }

            unsigned char bom[2] = { 0xFF, 0xFE };
            outFile.write((const char*)bom, 2);
            outFile.write((const char*)wOut.data(), wOut.size() * sizeof(wchar_t));
            outFile.close();
        }
        else
        {
            std::ofstream outFile(actFilePath, std::ios::binary | std::ios::trunc);
            if (!outFile.is_open())
            {
                result.errorMessage = L"Could not open activity file for writing.";
                return result;
            }
            outFile.write(finalContent.data(), finalContent.size());
            outFile.close();
        }

        result.success = true;
        return result;
    }
}
