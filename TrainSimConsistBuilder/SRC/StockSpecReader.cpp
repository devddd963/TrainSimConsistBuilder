#include "StockSpecReader.h"
#include "AssetsParser.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cmath>

namespace StockSpecReader
{
    // =========================================================================
    // String & Unicode Helpers
    // =========================================================================
    static std::string ReadFileRawBytes(const std::wstring& filePath)
    {
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return "";

        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == 0 || fileSize == INVALID_FILE_SIZE)
        {
            CloseHandle(hFile);
            return "";
        }

        std::vector<char> buffer(fileSize);
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL))
        {
            CloseHandle(hFile);
            return "";
        }
        CloseHandle(hFile);

        if (bytesRead >= 2 && (unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE)
        {
            // UTF-16LE
            int wlen = (bytesRead - 2) / 2;
            const wchar_t* pwc = (const wchar_t*)(buffer.data() + 2);
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pwc, wlen, NULL, 0, NULL, NULL);
            if (utf8Len > 0)
            {
                std::string utf8Str(utf8Len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, pwc, wlen, &utf8Str[0], utf8Len, NULL, NULL);
                return utf8Str;
            }
        }
        else if (bytesRead >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF)
        {
            // UTF-8 with BOM
            return std::string(buffer.data() + 3, bytesRead - 3);
        }

        return std::string(buffer.data(), bytesRead);
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

    static std::string ToLowerAscii(const std::string& str)
    {
        std::string s = str;
        for (char& c : s) c = (char)tolower((unsigned char)c);
        return s;
    }

    static std::wstring ToLowerWide(const std::wstring& str)
    {
        std::wstring s = str;
        for (wchar_t& c : s) c = towlower(c);
        return s;
    }

    static std::wstring NormalizePath(const std::wstring& path)
    {
        std::wstring res = path;
        for (wchar_t& c : res)
        {
            if (c == L'/') c = L'\\';
        }
        return res;
    }

    static std::wstring ResolvePathRelative(const std::wstring& baseDir, const std::wstring& relPath)
    {
        std::wstring normRel = NormalizePath(relPath);
        std::vector<std::wstring> parts;
        std::wstringstream baseSS(baseDir);
        std::wstring part;
        while (std::getline(baseSS, part, L'\\'))
        {
            if (!part.empty()) parts.push_back(part);
        }

        std::wstringstream relSS(normRel);
        while (std::getline(relSS, part, L'\\'))
        {
            if (part == L"." || part.empty()) continue;
            if (part == L"..")
            {
                if (!parts.empty()) parts.pop_back();
            }
            else
            {
                parts.push_back(part);
            }
        }

        std::wstring resolved;
        if (!baseDir.empty() && baseDir[0] == L'\\') resolved = L"\\";
        for (size_t i = 0; i < parts.size(); ++i)
        {
            resolved += parts[i];
            if (i + 1 < parts.size()) resolved += L"\\";
        }
        return resolved;
    }

    static std::wstring FindExistingStockPath(
        const std::wstring& folderDir,
        const std::wstring& relPath,
        const std::wstring& trainsetBasePath,
        const std::wstring& folderName,
        bool& outFound)
    {
        outFound = false;
        if (relPath.empty()) return L"";

        std::wstring normRel = NormalizePath(relPath);

        // Strip leading slashes to get clean relative path (handles //, \\, /, \)
        size_t firstChar = normRel.find_first_not_of(L"\\/");
        std::wstring cleanRel = (firstChar != std::wstring::npos) ? normRel.substr(firstChar) : normRel;

        // 1. Direct relative to current vehicle folder (handles local file, ./file, sub/file, //sub/file)
        std::wstring p1 = ResolvePathRelative(folderDir, cleanRel);
        if (GetFileAttributesW(p1.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            outFound = true;
            return p1;
        }

        // 2. Relative to parent of vehicle folder (TRAINSET level, e.g. //tboards/..., ../tboards/...)
        std::wstring p2 = ResolvePathRelative(folderDir + L"..\\", cleanRel);
        if (GetFileAttributesW(p2.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            outFound = true;
            return p2;
        }

        // 3. Relative using original normRel (handles ../.. chains)
        std::wstring p3 = ResolvePathRelative(folderDir, normRel);
        if (GetFileAttributesW(p3.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            outFound = true;
            return p3;
        }

        // 4. Using trainsetBasePath if provided
        if (!trainsetBasePath.empty())
        {
            if (!folderName.empty())
            {
                std::wstring p4a = ResolvePathRelative(trainsetBasePath + L"TRAINS\\TRAINSET\\" + folderName + L"\\", cleanRel);
                if (GetFileAttributesW(p4a.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    outFound = true;
                    return p4a;
                }
            }

            std::wstring p4b = ResolvePathRelative(trainsetBasePath + L"TRAINS\\TRAINSET\\", cleanRel);
            if (GetFileAttributesW(p4b.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                outFound = true;
                return p4b;
            }
        }

        // Default to p1 or p2 for display/copy
        return p1.empty() ? p2 : p1;
    }

    // =========================================================================
    // Unit Conversion Functions
    // =========================================================================
    float ParseDistanceUnit(const std::string& valStr, float defaultM)
    {
        if (valStr.empty()) return defaultM;
        std::string s = ToLowerAscii(valStr);

        float factor = 1.0f; // default is meters in MSTS/OR
        if (s.find("cm") != std::string::npos) { factor = 0.01f; s.erase(s.find("cm"), 2); }
        else if (s.find("mm") != std::string::npos) { factor = 0.001f; s.erase(s.find("mm"), 2); }
        else if (s.find("km") != std::string::npos) { factor = 1000.0f; s.erase(s.find("km"), 2); }
        else if (s.find("in") != std::string::npos) { factor = 0.0254f; s.erase(s.find("in"), 2); }
        else if (s.find("ft") != std::string::npos) { factor = 0.3048f; s.erase(s.find("ft"), 2); }
        else if (s.find("yd") != std::string::npos) { factor = 0.9144f; s.erase(s.find("yd"), 2); }
        else if (s.find("mi") != std::string::npos) { factor = 1609.344f; s.erase(s.find("mi"), 2); }
        else if (s.find("m") != std::string::npos) { factor = 1.0f; s.erase(s.find("m"), 1); }

        try {
            float val = std::stof(s);
            return val * factor;
        }
        catch (...) {
            return defaultM;
        }
    }

    float ParseForceUnit(const std::string& valStr, float defaultN)
    {
        if (valStr.empty()) return defaultN;
        std::string s = ToLowerAscii(valStr);

        float factor = 1.0f; // Newtons
        if (s.find("mn") != std::string::npos) { factor = 1e6f; s.erase(s.find("mn"), 2); }
        else if (s.find("kn") != std::string::npos) { factor = 1e3f; s.erase(s.find("kn"), 2); }
        else if (s.find("lbf") != std::string::npos) { factor = 4.44822f; s.erase(s.find("lbf"), 3); }
        else if (s.find("lb") != std::string::npos) { factor = 4.44822f; s.erase(s.find("lb"), 2); }
        else if (s.find("tonf") != std::string::npos) { factor = 8896.443f; s.erase(s.find("tonf"), 4); }
        else if (s.find("tf") != std::string::npos) { factor = 9806.65f; s.erase(s.find("tf"), 2); }
        else if (s.find("n") != std::string::npos) { factor = 1.0f; s.erase(s.find("n"), 1); }

        try {
            float val = std::stof(s);
            return val * factor;
        }
        catch (...) {
            return defaultN;
        }
    }

    float ParseMassUnit(const std::string& valStr, float defaultKg)
    {
        if (valStr.empty()) return defaultKg;
        std::string s = ToLowerAscii(valStr);

        float factor = 1000.0f; // MSTS Mass default is metric tons (1000 kg) or specified units
        if (s.find("tonne") != std::string::npos) { factor = 1000.0f; s.erase(s.find("tonne"), 5); }
        else if (s.find("tons") != std::string::npos) { factor = 1016.047f; s.erase(s.find("tons"), 4); }
        else if (s.find("ton") != std::string::npos) { factor = 1000.0f; s.erase(s.find("ton"), 3); }
        else if (s.find("kg") != std::string::npos) { factor = 1.0f; s.erase(s.find("kg"), 2); }
        else if (s.find("lbs") != std::string::npos) { factor = 0.453592f; s.erase(s.find("lbs"), 3); }
        else if (s.find("lb") != std::string::npos) { factor = 0.453592f; s.erase(s.find("lb"), 2); }
        else if (s.find("t") != std::string::npos) { factor = 1000.0f; s.erase(s.find("t"), 1); }

        try {
            float val = std::stof(s);
            return val * factor;
        }
        catch (...) {
            return defaultKg;
        }
    }

    float ParsePowerUnit(const std::string& valStr, float defaultKw)
    {
        if (valStr.empty()) return defaultKw;
        std::string s = ToLowerAscii(valStr);

        float factor = 1.0f; // kW
        if (s.find("hp") != std::string::npos) { factor = 0.7457f; s.erase(s.find("hp"), 2); }
        else if (s.find("mw") != std::string::npos) { factor = 1000.0f; s.erase(s.find("mw"), 2); }
        else if (s.find("kw") != std::string::npos) { factor = 1.0f; s.erase(s.find("kw"), 2); }
        else if (s.find("w") != std::string::npos) { factor = 0.001f; s.erase(s.find("w"), 1); }

        try {
            float val = std::stof(s);
            return val * factor;
        }
        catch (...) {
            return defaultKw;
        }
    }

    float ParseSpeedUnit(const std::string& valStr, float defaultKmh)
    {
        if (valStr.empty()) return defaultKmh;
        std::string s = ToLowerAscii(valStr);

        float factor = 1.0f; // km/h
        if (s.find("mph") != std::string::npos) { factor = 1.60934f; s.erase(s.find("mph"), 3); }
        else if (s.find("mi/h") != std::string::npos) { factor = 1.60934f; s.erase(s.find("mi/h"), 4); }
        else if (s.find("km/h") != std::string::npos) { factor = 1.0f; s.erase(s.find("km/h"), 4); }
        else if (s.find("kmh") != std::string::npos) { factor = 1.0f; s.erase(s.find("kmh"), 3); }
        else if (s.find("kph") != std::string::npos) { factor = 1.0f; s.erase(s.find("kph"), 3); }
        else if (s.find("m/s") != std::string::npos) { factor = 3.6f; s.erase(s.find("m/s"), 3); }
        else if (s.find("mps") != std::string::npos) { factor = 3.6f; s.erase(s.find("mps"), 3); }

        try {
            float val = std::stof(s);
            return val * factor;
        }
        catch (...) {
            return defaultKmh;
        }
    }

    // =========================================================================
    // STF Lexer & Parser Implementation
    // =========================================================================
    struct STFToken
    {
        std::string text;
        bool isString = false;
        bool isOpening = false;
        bool isClosing = false;
    };

    class STFLexer
    {
    private:
        std::string m_Content;
        size_t m_Pos = 0;

    public:
        STFLexer(const std::string& content) : m_Content(content), m_Pos(0) {}

        bool GetNextToken(STFToken& outToken)
        {
            while (m_Pos < m_Content.size())
            {
                char c = m_Content[m_Pos];

                // Whitespace
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0')
                {
                    m_Pos++;
                    continue;
                }

                // Line comment starting with # or //
                if (c == '#' || (c == '/' && m_Pos + 1 < m_Content.size() && m_Content[m_Pos + 1] == '/'))
                {
                    while (m_Pos < m_Content.size() && m_Content[m_Pos] != '\n') m_Pos++;
                    continue;
                }

                // Parentheses
                if (c == '(')
                {
                    m_Pos++;
                    outToken.text = "(";
                    outToken.isString = false;
                    outToken.isOpening = true;
                    outToken.isClosing = false;
                    return true;
                }
                if (c == ')')
                {
                    m_Pos++;
                    outToken.text = ")";
                    outToken.isString = false;
                    outToken.isOpening = false;
                    outToken.isClosing = true;
                    return true;
                }

                // Quoted string
                if (c == '"')
                {
                    m_Pos++;
                    size_t start = m_Pos;
                    while (m_Pos < m_Content.size() && m_Content[m_Pos] != '"')
                    {
                        if (m_Content[m_Pos] == '\\' && m_Pos + 1 < m_Content.size())
                        {
                            m_Pos += 2;
                        }
                        else
                        {
                            m_Pos++;
                        }
                    }
                    outToken.text = m_Content.substr(start, m_Pos - start);
                    if (m_Pos < m_Content.size() && m_Content[m_Pos] == '"') m_Pos++;
                    outToken.isString = true;
                    outToken.isOpening = false;
                    outToken.isClosing = false;
                    return true;
                }

                // Unquoted token
                size_t start = m_Pos;
                while (m_Pos < m_Content.size())
                {
                    char ch = m_Content[m_Pos];
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '(' || ch == ')' || ch == '"' || ch == '#' || ch == '\0')
                    {
                        break;
                    }
                    m_Pos++;
                }

                outToken.text = m_Content.substr(start, m_Pos - start);
                outToken.isString = false;
                outToken.isOpening = false;
                outToken.isClosing = false;
                return true;
            }
            return false;
        }

        void SkipBlock()
        {
            int depth = 1;
            STFToken tok;
            while (GetNextToken(tok))
            {
                if (tok.isOpening) depth++;
                else if (tok.isClosing)
                {
                    depth--;
                    if (depth <= 0) break;
                }
            }
        }
    };

    // =========================================================================
    // Recursive Spec Collector
    // =========================================================================
    static void ParseSTFStream(
        const std::wstring& currentFilePath,
        StockSpec& spec,
        int depth,
        const std::wstring& trainsetBasePath)
    {
        if (depth > 12) return;

        std::string rawData = ReadFileRawBytes(currentFilePath);
        if (rawData.empty())
        {
            spec.missingIncludes.push_back(currentFilePath);
            return;
        }

        if (depth > 0)
        {
            spec.resolvedIncludes.push_back(currentFilePath);
        }

        size_t lastSlash = currentFilePath.find_last_of(L"\\/");
        std::wstring currentDir = (lastSlash != std::wstring::npos) ? currentFilePath.substr(0, lastSlash + 1) : L"";
        std::wstring currentBaseName = (lastSlash != std::wstring::npos) ? currentFilePath.substr(lastSlash + 1) : currentFilePath;
        size_t dotPos = currentBaseName.find_last_of(L'.');
        std::wstring currentStem = (dotPos != std::wstring::npos) ? currentBaseName.substr(0, dotPos) : currentBaseName;

        STFLexer lexer(rawData);
        STFToken tok;

        std::vector<std::string> blockStack;

        while (lexer.GetNextToken(tok))
        {
            if (tok.isOpening)
            {
                // Entering a block
                continue;
            }
            if (tok.isClosing)
            {
                if (!blockStack.empty()) blockStack.pop_back();
                continue;
            }

            std::string lowerTok = ToLowerAscii(tok.text);

            // Handle include directive
            if (lowerTok == "include")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string incPathStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken pathTok;
                        if (lexer.GetNextToken(pathTok))
                        {
                            incPathStr = pathTok.text;
                        }
                        lexer.SkipBlock();
                    }
                    else
                    {
                        incPathStr = nextTok.text;
                    }

                    if (!incPathStr.empty())
                    {
                        std::wstring incRelPath = ToWide(incPathStr);
                        // Handle [[samename]] wildcard
                        size_t samePos = ToLowerWide(incRelPath).find(L"[[samename]]");
                        if (samePos != std::wstring::npos)
                        {
                            incRelPath.replace(samePos, 12, currentStem + L".inc");
                        }

                        bool incFound = false;
                        std::wstring incFullPath = FindExistingStockPath(currentDir, incRelPath, trainsetBasePath, currentStem, incFound);
                        if (incFound)
                        {
                            ParseSTFStream(incFullPath, spec, depth + 1, trainsetBasePath);
                        }
                        else
                        {
                            spec.missingIncludes.push_back(incFullPath.empty() ? incRelPath : incFullPath);
                        }
                    }
                }
                continue;
            }

            // Skip comments and skips
            if (lowerTok == "comment" || lowerTok == "skip")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    if (nextTok.isOpening)
                    {
                        lexer.SkipBlock();
                    }
                }
                continue;
            }

            // Wagon name
            if (lowerTok == "wagon" && blockStack.empty())
            {
                blockStack.push_back("wagon");
                STFToken nameTok;
                if (lexer.GetNextToken(nameTok))
                {
                    if (!nameTok.isOpening && !nameTok.isClosing)
                    {
                        if (spec.wagonName.empty()) spec.wagonName = ToWide(nameTok.text);
                    }
                }
                continue;
            }

            // Engine name
            if (lowerTok == "engine" && (blockStack.empty() || blockStack.back() == "wagon"))
            {
                blockStack.push_back("engine");
                STFToken nameTok;
                if (lexer.GetNextToken(nameTok))
                {
                    if (!nameTok.isOpening && !nameTok.isClosing)
                    {
                        if (spec.engineName.empty()) spec.engineName = ToWide(nameTok.text);
                    }
                }
                continue;
            }

            // Display Name
            if (lowerTok == "name" && spec.displayName.empty())
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok))
                        {
                            spec.displayName = ToWide(valTok.text);
                        }
                        lexer.SkipBlock();
                    }
                    else
                    {
                        spec.displayName = ToWide(nextTok.text);
                    }
                }
                continue;
            }

            // Type
            if (lowerTok == "type")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string typeVal = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) typeVal = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        typeVal = nextTok.text;
                    }

                    if (!typeVal.empty())
                    {
                        std::string tLow = ToLowerAscii(typeVal);
                        if (tLow == "diesel" || tLow == "diesel-electric" || tLow == "diesel-hydraulic" || tLow == "hydraulic")
                        {
                            spec.category = L"Diesel";
                            spec.rawType = L"Diesel";
                        }
                        else if (tLow == "electric" || tLow == "electro-diesel")
                        {
                            spec.category = L"Electric";
                            spec.rawType = L"Electric";
                        }
                        else if (tLow == "steam")
                        {
                            spec.category = L"Steam";
                            spec.rawType = L"Steam";
                        }
                        else if (tLow == "control" || tLow == "cab" || tLow == "dvt")
                        {
                            spec.category = L"Control";
                            spec.rawType = L"Control";
                        }
                        else if (tLow == "passenger" || tLow == "carriage" || tLow == "coach")
                        {
                            if (spec.category.empty() || spec.category == L"Freight")
                            {
                                spec.category = L"Passenger";
                                spec.rawType = L"Passenger";
                            }
                        }
                        else if (tLow == "freight" || tLow == "wagon")
                        {
                            if (spec.category.empty())
                            {
                                spec.category = L"Freight";
                                spec.rawType = L"Freight";
                            }
                        }
                        else if (tLow == "tender")
                        {
                            spec.category = L"Tender";
                            spec.rawType = L"Tender";
                        }
                        else
                        {
                            if (spec.rawType.empty()) spec.rawType = ToWide(typeVal);
                        }
                    }
                }
                continue;
            }

            // Size ( Width Height Length )
            if (lowerTok == "size")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    STFToken wTok, hTok, lTok;
                    if (lexer.GetNextToken(wTok) && lexer.GetNextToken(hTok) && lexer.GetNextToken(lTok))
                    {
                        spec.size.widthM = ParseDistanceUnit(wTok.text, 3.0f);
                        spec.size.heightM = ParseDistanceUnit(hTok.text, 4.0f);
                        spec.size.lengthM = ParseDistanceUnit(lTok.text, 15.0f);
                    }
                    lexer.SkipBlock();
                }
                continue;
            }

            // Mass
            if (lowerTok == "mass")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string massStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) massStr = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        massStr = nextTok.text;
                    }
                    if (spec.massKg == 0.0f)
                    {
                        spec.massKg = ParseMassUnit(massStr, 0.0f);
                    }
                }
                continue;
            }

            // MaxPower
            if (lowerTok == "maxpower")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string pStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) pStr = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        pStr = nextTok.text;
                    }
                    if (spec.maxPowerKw == 0.0f)
                    {
                        spec.maxPowerKw = ParsePowerUnit(pStr, 0.0f);
                    }
                }
                continue;
            }

            // MaxForce
            if (lowerTok == "maxforce")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string fStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) fStr = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        fStr = nextTok.text;
                    }
                    if (spec.maxForceKn == 0.0f)
                    {
                        spec.maxForceKn = ParseForceUnit(fStr, 0.0f) / 1000.0f;
                    }
                }
                continue;
            }

            // MaxVelocity
            if (lowerTok == "maxvelocity")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string vStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) vStr = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        vStr = nextTok.text;
                    }
                    if (spec.maxVelocityKmh == 0.0f)
                    {
                        spec.maxVelocityKmh = ParseSpeedUnit(vStr, 0.0f);
                    }
                }
                continue;
            }

            // WagonShape
            if (lowerTok == "wagonshape")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string shapeStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) shapeStr = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        shapeStr = nextTok.text;
                    }
                    if (spec.mainShapeFile.empty())
                    {
                        spec.mainShapeFile = ToWide(shapeStr);
                    }
                }
                continue;
            }

            // FreightAnim / ORTSFreightAnims / ORTSFreightAnim
            if (lowerTok == "freightanim" || lowerTok == "ortsfreightanim" || lowerTok == "ortsfreightanims")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    int faDepth = 1;
                    STFToken fTok;
                    bool firstTokenInBlock = true;
                    bool isClassicMSTS = (lowerTok == "freightanim");

                    while (lexer.GetNextToken(fTok))
                    {
                        if (fTok.isOpening)
                        {
                            faDepth++;
                            continue;
                        }
                        if (fTok.isClosing)
                        {
                            faDepth--;
                            if (faDepth <= 0) break;
                            continue;
                        }

                        std::string fLow = ToLowerAscii(fTok.text);

                        // 1. If we encounter Shape keyword inside any sub-block (Open Rails style)
                        if (fLow == "shape")
                        {
                            STFToken shapeTok;
                            if (lexer.GetNextToken(shapeTok))
                            {
                                std::string shapePathStr = "";
                                if (shapeTok.isOpening)
                                {
                                    STFToken valTok;
                                    if (lexer.GetNextToken(valTok))
                                    {
                                        shapePathStr = valTok.text;
                                    }
                                    lexer.SkipBlock();
                                }
                                else
                                {
                                    shapePathStr = shapeTok.text;
                                }

                                if (!shapePathStr.empty())
                                {
                                    StockFreightAnim fa;
                                    fa.shapePath = ToWide(shapePathStr);
                                    spec.freightAnims.push_back(fa);
                                }
                            }
                            firstTokenInBlock = false;
                        }
                        // 2. Classic MSTS format where the first parameter directly inside FreightAnim ( ... ) is the shape name
                        else if (isClassicMSTS && faDepth == 1 && firstTokenInBlock)
                        {
                            // Verify it's not a keyword for nested ORTS block
                            if (fLow != "freightanimstatic" && fLow != "freightanimcontinuous" &&
                                fLow != "mstsfreightanim" && fLow != "freightanimadded" && fLow != "freightanimdriver")
                            {
                                std::string shapeName = fTok.text;
                                if (shapeName.length() < 2 || shapeName.rfind(".s") != shapeName.length() - 2)
                                {
                                    // If author omitted .s extension, ensure .s is included
                                    if (shapeName.find('.') == std::string::npos)
                                    {
                                        shapeName += ".s";
                                    }
                                }
                                StockFreightAnim fa;
                                fa.shapePath = ToWide(shapeName);
                                spec.freightAnims.push_back(fa);
                            }
                            firstTokenInBlock = false;
                        }
                        // 3. Fallback: Any token ending in .s or containing slashes inside any block
                        else if (fLow.length() > 2 && (fLow.rfind(".s") == fLow.length() - 2 || fLow.find('/') != std::string::npos || fLow.find('\\') != std::string::npos))
                        {
                            std::wstring wideShape = ToWide(fTok.text);
                            bool exists = false;
                            for (const auto& existing : spec.freightAnims)
                            {
                                if (existing.shapePath == wideShape) { exists = true; break; }
                            }
                            if (!exists)
                            {
                                StockFreightAnim fa;
                                fa.shapePath = wideShape;
                                spec.freightAnims.push_back(fa);
                            }
                            firstTokenInBlock = false;
                        }
                        else
                        {
                            firstTokenInBlock = false;
                        }
                    }
                }
                continue;
            }

            // CabView
            if (lowerTok == "cabview")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok))
                {
                    std::string cvStr = "";
                    if (nextTok.isOpening)
                    {
                        STFToken valTok;
                        if (lexer.GetNextToken(valTok)) cvStr = valTok.text;
                        lexer.SkipBlock();
                    }
                    else
                    {
                        cvStr = nextTok.text;
                    }
                    if (spec.cabViewFile.empty())
                    {
                        spec.cabViewFile = ToWide(cvStr);
                    }
                }
                continue;
            }

            // Coupling
            if (lowerTok == "coupling")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    StockCoupler coupler;
                    int subDepth = 1;
                    STFToken cTok;
                    while (lexer.GetNextToken(cTok))
                    {
                        if (cTok.isOpening) { subDepth++; continue; }
                        if (cTok.isClosing)
                        {
                            subDepth--;
                            if (subDepth <= 0) break;
                            continue;
                        }

                        std::string cLow = ToLowerAscii(cTok.text);
                        if (cLow == "type")
                        {
                            STFToken tTok;
                            if (lexer.GetNextToken(tTok))
                            {
                                if (tTok.isOpening)
                                {
                                    STFToken valTok;
                                    if (lexer.GetNextToken(valTok)) coupler.type = ToWide(valTok.text);
                                    lexer.SkipBlock();
                                }
                                else coupler.type = ToWide(tTok.text);
                            }
                        }
                        else if (cLow == "couplinghasrigidconnection")
                        {
                            coupler.hasRigidConnection = true;
                        }
                        else if (cLow == "velocity")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok))
                            {
                                std::string vStr = "";
                                if (oTok.isOpening)
                                {
                                    STFToken valTok;
                                    if (lexer.GetNextToken(valTok)) vStr = valTok.text;
                                    lexer.SkipBlock();
                                }
                                else vStr = oTok.text;
                                coupler.rawVelocity = ToWide(vStr);
                                coupler.velocity = ParseSpeedUnit(vStr, 0.15f) / 3.6f;
                            }
                        }
                        else if (cLow == "r0")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken minTok, maxTok;
                                if (lexer.GetNextToken(minTok))
                                {
                                    coupler.r0_min = ParseDistanceUnit(minTok.text, 0.15f);
                                    coupler.rawR0 = ToWide(minTok.text);
                                }
                                if (lexer.GetNextToken(maxTok))
                                {
                                    coupler.r0_max = ParseDistanceUnit(maxTok.text, coupler.r0_min);
                                    coupler.rawR0 += L" " + ToWide(maxTok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                        else if (cLow == "stiffness")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken s1Tok, s2Tok;
                                if (lexer.GetNextToken(s1Tok))
                                {
                                    coupler.stiffness1 = ParseForceUnit(s1Tok.text, 5e6f);
                                    coupler.rawStiffness = ToWide(s1Tok.text);
                                }
                                if (lexer.GetNextToken(s2Tok))
                                {
                                    coupler.stiffness2 = ParseForceUnit(s2Tok.text, 0.0f);
                                    coupler.rawStiffness += L" " + ToWide(s2Tok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                        else if (cLow == "damping")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken d1Tok, d2Tok;
                                if (lexer.GetNextToken(d1Tok))
                                {
                                    coupler.damping1 = ParseForceUnit(d1Tok.text, 1e6f);
                                    coupler.rawDamping = ToWide(d1Tok.text);
                                }
                                if (lexer.GetNextToken(d2Tok))
                                {
                                    coupler.damping2 = ParseForceUnit(d2Tok.text, 0.0f);
                                    coupler.rawDamping += L" " + ToWide(d2Tok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                        else if (cLow == "break")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken b1Tok, b2Tok;
                                if (lexer.GetNextToken(b1Tok))
                                {
                                    coupler.break1 = ParseForceUnit(b1Tok.text, 5.2e6f);
                                    coupler.rawBreak = ToWide(b1Tok.text);
                                }
                                if (lexer.GetNextToken(b2Tok))
                                {
                                    coupler.break2 = ParseForceUnit(b2Tok.text, 5.2e6f);
                                    coupler.rawBreak += L" " + ToWide(b2Tok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                    }
                    spec.couplers.push_back(coupler);
                }
                continue;
            }

            // Buffers
            if (lowerTok == "buffers")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    spec.buffers.exists = true;
                    int subDepth = 1;
                    STFToken bTok;
                    while (lexer.GetNextToken(bTok))
                    {
                        if (bTok.isOpening) { subDepth++; continue; }
                        if (bTok.isClosing)
                        {
                            subDepth--;
                            if (subDepth <= 0) break;
                            continue;
                        }

                        std::string bLow = ToLowerAscii(bTok.text);
                        if (bLow == "r0")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken minTok, maxTok;
                                if (lexer.GetNextToken(minTok))
                                {
                                    spec.buffers.r0_min = ParseDistanceUnit(minTok.text, 0.0f);
                                    spec.buffers.rawR0 = ToWide(minTok.text);
                                }
                                if (lexer.GetNextToken(maxTok))
                                {
                                    spec.buffers.r0_max = ParseDistanceUnit(maxTok.text, 1e9f);
                                    spec.buffers.rawR0 += L" " + ToWide(maxTok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                        else if (bLow == "stiffness")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken s1Tok, s2Tok;
                                if (lexer.GetNextToken(s1Tok))
                                {
                                    spec.buffers.stiffness1 = ParseForceUnit(s1Tok.text, 5e6f);
                                    spec.buffers.rawStiffness = ToWide(s1Tok.text);
                                }
                                if (lexer.GetNextToken(s2Tok))
                                {
                                    spec.buffers.stiffness2 = ParseForceUnit(s2Tok.text, spec.buffers.stiffness1);
                                    spec.buffers.rawStiffness += L" " + ToWide(s2Tok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                        else if (bLow == "damping")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken d1Tok, d2Tok;
                                if (lexer.GetNextToken(d1Tok))
                                {
                                    spec.buffers.damping1 = ParseForceUnit(d1Tok.text, 1e6f);
                                    spec.buffers.rawDamping = ToWide(d1Tok.text);
                                }
                                if (lexer.GetNextToken(d2Tok))
                                {
                                    spec.buffers.damping2 = ParseForceUnit(d2Tok.text, spec.buffers.damping1);
                                    spec.buffers.rawDamping += L" " + ToWide(d2Tok.text);
                                }
                                lexer.SkipBlock();
                            }
                        }
                        else if (bLow == "centre")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken cTok2;
                                if (lexer.GetNextToken(cTok2)) spec.buffers.centre = ParseDistanceUnit(cTok2.text, 0.5f);
                                lexer.SkipBlock();
                            }
                        }
                        else if (bLow == "radius")
                        {
                            STFToken oTok;
                            if (lexer.GetNextToken(oTok) && oTok.isOpening)
                            {
                                STFToken rTok;
                                if (lexer.GetNextToken(rTok)) spec.buffers.radius = ParseDistanceUnit(rTok.text, 1.0f);
                                lexer.SkipBlock();
                            }
                        }
                    }
                }
                continue;
            }

            // ORTS Extended Physics
            if (lowerTok == "ortslengthbogiecentre")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    STFToken vTok;
                    if (lexer.GetNextToken(vTok)) spec.ortsLengthBogieCentreM = ParseDistanceUnit(vTok.text, 0.0f);
                    lexer.SkipBlock();
                }
                continue;
            }
            if (lowerTok == "ortslengthcarbody")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    STFToken vTok;
                    if (lexer.GetNextToken(vTok)) spec.ortsLengthCarBodyM = ParseDistanceUnit(vTok.text, 0.0f);
                    lexer.SkipBlock();
                }
                continue;
            }
            if (lowerTok == "ortslengthcouplerface")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    STFToken vTok;
                    if (lexer.GetNextToken(vTok)) spec.ortsLengthCouplerFaceM = ParseDistanceUnit(vTok.text, 0.0f);
                    lexer.SkipBlock();
                }
                continue;
            }
            if (lowerTok == "ortslengthairhose")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    STFToken vTok;
                    if (lexer.GetNextToken(vTok)) spec.ortsLengthAirHoseM = ParseDistanceUnit(vTok.text, 0.0f);
                    lexer.SkipBlock();
                }
                continue;
            }
            if (lowerTok == "ortswheelflangelength")
            {
                STFToken nextTok;
                if (lexer.GetNextToken(nextTok) && nextTok.isOpening)
                {
                    STFToken vTok;
                    if (lexer.GetNextToken(vTok)) spec.ortsWheelFlangeLengthM = ParseDistanceUnit(vTok.text, 0.0f);
                    lexer.SkipBlock();
                }
                continue;
            }
        }
    }

    // =========================================================================
    // Public API Implementation
    // =========================================================================
    StockSpec ReadFullSpec(const std::wstring& filePath, const std::wstring& trainsetBasePath)
    {
        StockSpec spec;
        spec.filePath = filePath;

        size_t lastSlash = filePath.find_last_of(L"\\/");
        spec.fileName = (lastSlash != std::wstring::npos) ? filePath.substr(lastSlash + 1) : filePath;
        
        size_t prevSlash = (lastSlash != std::wstring::npos && lastSlash > 0) ? filePath.find_last_of(L"\\/", lastSlash - 1) : std::wstring::npos;
        if (prevSlash != std::wstring::npos && lastSlash > prevSlash)
        {
            spec.folderName = filePath.substr(prevSlash + 1, lastSlash - prevSlash - 1);
        }

        size_t dotPos = spec.fileName.find_last_of(L'.');
        if (dotPos != std::wstring::npos)
        {
            spec.extension = ToLowerWide(spec.fileName.substr(dotPos));
        }

        DWORD fileAttr = GetFileAttributesW(filePath.c_str());
        spec.fileExistsOnDisk = (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY));

        if (spec.fileExistsOnDisk)
        {
            ParseSTFStream(filePath, spec, 0, trainsetBasePath);
            spec.isValid = true;
        }
        else
        {
            spec.isValid = false;
            spec.missingIncludes.push_back(filePath);
        }

        // Derive category if not explicitly found in Type block
        if (spec.category.empty() || spec.category == L"Engine" || spec.category == L"Wagon")
        {
            if (spec.extension == L".eng")
            {
                if (ParseEnginePropulsionType(filePath, spec.category))
                {
                    spec.rawType = spec.category;
                }
                else
                {
                    std::wstring lowFile = ToLowerWide(spec.fileName + L" " + spec.folderName);
                    if (lowFile.find(L"wap") != std::wstring::npos || lowFile.find(L"wag") != std::wstring::npos || lowFile.find(L"wam") != std::wstring::npos || lowFile.find(L"wcam") != std::wstring::npos || lowFile.find(L"elec") != std::wstring::npos)
                    {
                        spec.category = L"Electric";
                    }
                    else if (lowFile.find(L"wdg") != std::wstring::npos || lowFile.find(L"wdp") != std::wstring::npos || lowFile.find(L"wdm") != std::wstring::npos || lowFile.find(L"wds") != std::wstring::npos || lowFile.find(L"diesel") != std::wstring::npos)
                    {
                        spec.category = L"Diesel";
                    }
                    else if (lowFile.find(L"steam") != std::wstring::npos || lowFile.find(L"wp") != std::wstring::npos || lowFile.find(L"wg") != std::wstring::npos || lowFile.find(L"yp") != std::wstring::npos || lowFile.find(L"yg") != std::wstring::npos)
                    {
                        spec.category = L"Steam";
                    }
                    else
                    {
                        spec.category = L"Unresolved";
                    }
                }
            }
            else // .wag
            {
                if (ParseWagonType(filePath, spec.category))
                {
                    spec.rawType = spec.category;
                }
                else
                {
                    std::wstring lowFile = ToLowerWide(spec.fileName + L" " + spec.folderName);
                    if (lowFile.find(L"tender") != std::wstring::npos) spec.category = L"Tender";
                    else if (lowFile.find(L"lhb") != std::wstring::npos || lowFile.find(L"icf") != std::wstring::npos || lowFile.find(L"coach") != std::wstring::npos || lowFile.find(L"sl") != std::wstring::npos || lowFile.find(L"3a") != std::wstring::npos || lowFile.find(L"2a") != std::wstring::npos || lowFile.find(L"pass") != std::wstring::npos)
                    {
                        spec.category = L"Passenger";
                    }
                    else
                    {
                        spec.category = L"Unresolved";
                    }
                }
            }
        }

        // Display Name fallback
        if (spec.displayName.empty())
        {
            spec.displayName = (!spec.wagonName.empty()) ? spec.wagonName : ((dotPos != std::wstring::npos) ? spec.fileName.substr(0, dotPos) : spec.fileName);
        }

        // Locate Primary Shape File on Disk
        std::wstring folderDir = (lastSlash != std::wstring::npos) ? filePath.substr(0, lastSlash + 1) : L"";
        if (!spec.mainShapeFile.empty())
        {
            spec.fullShapePath = FindExistingStockPath(folderDir, spec.mainShapeFile, trainsetBasePath, spec.folderName, spec.shapeExistsOnDisk);
        }

        // Locate Freight Animation Shapes on Disk (supports ./, //, ../, subfolders, trainset shared folders)
        for (auto& fa : spec.freightAnims)
        {
            if (fa.shapePath.empty()) continue;
            fa.fullPath = FindExistingStockPath(folderDir, fa.shapePath, trainsetBasePath, spec.folderName, fa.existsOnDisk);
        }

        spec.isValid = true;
        return spec;
    }

    bool ReadSummary(
        const std::wstring& filePath,
        std::wstring& outName,
        std::wstring& outType,
        std::wstring& outPower,
        std::wstring& outMass,
        std::wstring& outCabView,
        const std::wstring& trainsetBasePath)
    {
        StockSpec spec = ReadFullSpec(filePath, trainsetBasePath);
        if (!spec.isValid) return false;

        outName = spec.displayName;
        outType = spec.category;

        if (spec.maxPowerKw > 0.0f)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"%.0f kW", spec.maxPowerKw);
            outPower = buf;
        }

        if (spec.massKg > 0.0f)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"%.1f t", spec.massKg / 1000.0f);
            outMass = buf;
        }

        outCabView = spec.cabViewFile;
        return true;
    }
}
