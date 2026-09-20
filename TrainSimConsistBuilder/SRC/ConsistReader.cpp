#include "ConsistReader.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cstddef>

namespace ConsistReader {
    // Helper to trim whitespace from a std::wstring.
    static std::wstring Trim(const std::wstring& s) {
        const wchar_t* ws = L" \t\r\n";
        size_t start = s.find_first_not_of(ws);
        size_t end   = s.find_last_not_of(ws);
        return (start == std::wstring::npos) ? L"" : s.substr(start, end - start + 1);
    }

    // Returns true if `lower` (already lowercased) contains `keyword` immediately
    // followed by optional spaces/tabs and then '(' — and does NOT contain the
    // longer token `keyword + "data"` on the same line.
    // This distinguishes  "Engine ("  from  "EngineData ("  and
    //                      "Wagon ("  from  "WagonData (".
    static bool IsBlockOpener(const std::string& lower, const char* keyword, const char* dataKeyword)
    {
        // Reject lines that contain the "data" variant (EngineData / WagonData)
        if (lower.find(dataKeyword) != std::string::npos) return false;

        size_t pos = lower.find(keyword);
        while (pos != std::string::npos)
        {
            size_t idx = pos + strlen(keyword);
            // Skip spaces/tabs
            while (idx < lower.size() && (lower[idx] == ' ' || lower[idx] == '\t')) idx++;
            if (idx < lower.size() && lower[idx] == '(')
                return true;
            pos = lower.find(keyword, pos + 1);
        }
        return false;
    }

    // Extract the first two whitespace-separated tokens from the content of a
    // EngineData / WagonData line.  Handles all four real-world formats:
    //
    //   EngineData ( "SRC_WAP4_2" "Wap1" )   → "SRC_WAP4_2",  "Wap1"
    //   EngineData ( SRC_WAP4_2 Wap1 )        → "SRC_WAP4_2",  "Wap1"
    //   EngineData ( "SRC WAP 4 2" Wap1 )     → "SRC WAP 4 2", "Wap1"
    //   EngineData ("SRC_WAP4_2""Wap1")       → "SRC_WAP4_2",  "Wap1"
    //   EngineData ( "ENG" "" )               → "ENG",          ""
    //
    // Rule: if the next non-whitespace char after '(' (or after the previous
    // token) is '"', read until the matching '"'.  Otherwise read until
    // whitespace, ')' or end-of-string.
    static void ExtractTwoTokens(const std::string& line,
                                  std::wstring& outFirst, std::wstring& outSecond)
    {
        // Find the opening '(' of the data block.
        size_t pos = line.find('(');
        if (pos == std::string::npos) return;
        ++pos; // step past '('

        const size_t len = line.size();

        // Generic single-token reader.  Advances `pos` past the token.
        auto readToken = [&](std::wstring& out) -> bool
        {
            // Skip leading whitespace
            while (pos < len && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

            if (pos >= len || line[pos] == ')') return false; // nothing left

            if (line[pos] == '"')
            {
                // Quoted token — read until closing '"'
                ++pos; // skip opening quote
                size_t start = pos;
                while (pos < len && line[pos] != '"') ++pos;
                out = std::wstring(line.begin() + start, line.begin() + pos);
                if (pos < len) ++pos; // skip closing quote
            }
            else
            {
                // Unquoted token — read until whitespace, ')' or end
                size_t start = pos;
                while (pos < len && line[pos] != ' ' && line[pos] != '\t'
                                  && line[pos] != ')' && line[pos] != '"') ++pos;
                out = std::wstring(line.begin() + start, line.begin() + pos);
            }
            return true;
        };

        readToken(outFirst);
        readToken(outSecond);
    }

    // ---------------------------------------------------------------------
    // Load a .con file and return a populated ConsistData struct.
    ConsistData LoadConsist(const std::wstring& fullPath) {
        std::string ascii = ReadConFileToAscii(fullPath);
        ConsistData data;

        // Derive base filename without extension.
        auto deriveFileName = [] (const std::wstring& p) -> std::wstring {
            std::wstring f = p;
            size_t pos = f.find_last_of(L"\\/");
            if (pos != std::wstring::npos) f = f.substr(pos + 1);
            size_t dot = f.find_last_of(L'.');
            if (dot != std::wstring::npos) f = f.substr(0, dot);
            return f;
        };
        data.fileName = deriveFileName(fullPath);

        // Use the existing unit-count helper (counts Engine/Wagon blocks).
        data.totalUnits = CountCarsInAscii(ascii);

        // -----------------------------------------------------------------
        // Pass 1 – TrainCfg header (trainCfgId, Name, MaxVelocity / PerfFactor).
        // -----------------------------------------------------------------
        {
            std::istringstream iss(ascii);
            std::string line;
            bool inCfg = false;
            while (std::getline(iss, line))
            {
                std::string l = line;
                std::transform(l.begin(), l.end(), l.begin(), ::tolower);

                if (!inCfg)
                {
                    if (l.find("traincfg") != std::string::npos && l.find('(') != std::string::npos)
                    {
                        // Extract the consist identifier — the first token after '(' on this line.
                        // Handles: TrainCfg ( NewConsist   and   TrainCfg ( "My Consist"
                        std::wstring dummy;
                        ExtractTwoTokens(line, data.trainCfg.trainCfgId, dummy);
                        inCfg = true;
                    }
                    continue;
                }

                // Stop once we hit the first Engine ( or Wagon ( block opener
                if (IsBlockOpener(l, "engine", "enginedata") ||
                    IsBlockOpener(l, "wagon",  "wagondata"))
                    break;

                // Name ( "Display Name" )
                if (l.find("name") != std::string::npos && l.find('(') != std::string::npos)
                {
                    size_t q1 = line.find('"');
                    size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
                    if (q1 != std::string::npos && q2 != std::string::npos)
                        data.trainCfg.name = std::wstring(line.begin() + q1 + 1, line.begin() + q2);
                }
                // MaxVelocity ( speed_m_per_s  perf_fraction )
                // The .con format stores speed in m/s — multiply by 3.6 to get km/h.
                else if (l.find("maxvelocity") != std::string::npos)
                {
                    double sp = 0.0, pf = 0.0;
                    size_t op = line.find('(');
                    if (op != std::string::npos)
                        sscanf_s(line.c_str() + op + 1, "%lf %lf", &sp, &pf);
                    data.trainCfg.maxVelocity = sp * 3.6;   // m/s → km/h
                    data.trainCfg.perfFactor  = pf * 100.0; // fraction → %
                }
            }
        }


        // -----------------------------------------------------------------
        // Pass 2 – Block-aware unit scanner.
        //
        // Mirrors ConsistWriter.cs structure:
        //
        //   Engine (                    ← outer opener  → isEngine = true
        //       EngineData ( "x" "y" ) ← data line     → uid, parentDir
        //       UiD ( N )              ← IGNORED completely
        //   )                          ← depth → 0     → emit UnitInfo
        //
        //   Wagon (
        //       WagonData ( "x" "y" )
        //       UiD ( N )              ← IGNORED
        //   )
        //
        // UiD order is irrelevant; physical block order is the consist sequence.
        // -----------------------------------------------------------------
        enum class ScanState { OUTSIDE, INSIDE_UNIT };
        ScanState state   = ScanState::OUTSIDE;
        bool  currentIsEngine = false;
        std::wstring currentUid, currentParentDir;
        bool  currentIsFlipped = false;
        int   depth = 0;

        std::istringstream iss2(ascii);
        std::string line;

        while (std::getline(iss2, line))
        {
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (state == ScanState::OUTSIDE)
            {
                // Detect outer Engine ( or Wagon ( block openers only.
                if (IsBlockOpener(lower, "engine", "enginedata"))
                {
                    state           = ScanState::INSIDE_UNIT;
                    currentIsEngine = true;
                    currentUid.clear();
                    currentParentDir.clear();
                    currentIsFlipped = false;
                    depth = 1; // the opener itself contributes 1 open paren
                }
                else if (IsBlockOpener(lower, "wagon", "wagondata"))
                {
                    state           = ScanState::INSIDE_UNIT;
                    currentIsEngine = false;
                    currentUid.clear();
                    currentParentDir.clear();
                    currentIsFlipped = false;
                    depth = 1;
                }
                // All other lines outside a unit block are ignored.
            }
            else // ScanState::INSIDE_UNIT
            {
                // Collect EngineData / WagonData — the actual unit file reference.
                if (lower.find("enginedata") != std::string::npos ||
                    lower.find("wagondata")  != std::string::npos)
                {
                    ExtractTwoTokens(line, currentUid, currentParentDir);
                }
                if (lower.find("flip") != std::string::npos)
                {
                    currentIsFlipped = true;
                }
                // UiD lines are completely ignored (no else-if for "uid").

                // Track paren depth to know when the outer block closes.
                for (char c : line)
                {
                    if      (c == '(') ++depth;
                    else if (c == ')') --depth;
                }

                // Depth == 0 → outer closing ')' reached → emit unit.
                if (depth <= 0)
                {
                    UnitInfo ui;
                    ui.uid       = currentUid;
                    ui.parentDir = currentParentDir;
                    ui.isEngine  = currentIsEngine;
                    ui.isFlipped = currentIsFlipped;
                    data.units.push_back(ui);

                    if (currentIsEngine)
                    {
                        EngineInfo ei;
                        ei.uid       = currentUid;
                        ei.parentDir = currentParentDir;
                        ei.isFlipped = currentIsFlipped;
                        data.engines.push_back(std::move(ei));
                    }
                    else
                    {
                        WagonInfo wi;
                        wi.uid       = currentUid;
                        wi.parentDir = currentParentDir;
                        wi.isFlipped = currentIsFlipped;
                        data.wagons.push_back(std::move(wi));
                    }

                    state = ScanState::OUTSIDE;
                    depth = 0;
                }
            }
        }

        return data;
    }
} // namespace ConsistReader

int CountCarsInAscii(const std::string& ascii)
{
    int count = 0;
    std::string lower = ascii;
    for (char& c : lower) c = tolower(c);

    // Count "Engine (" blocks
    size_t pos = lower.find("engine");
    while (pos != std::string::npos)
    {
        size_t idx = pos + 6;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n'))
        {
            idx++;
        }
        if (idx < lower.length() && lower[idx] == '(')
        {
            count++;
        }
        pos = lower.find("engine", pos + 1);
    }

    // Count "Wagon (" blocks
    pos = lower.find("wagon");
    while (pos != std::string::npos)
    {
        size_t idx = pos + 5;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n'))
        {
            idx++;
        }
        if (idx < lower.length() && lower[idx] == '(')
        {
            count++;
        }
        pos = lower.find("wagon", pos + 1);
    }

    return count;
}

std::wstring ExtractConsistName(const std::string& ascii)
{
    std::string lower = ascii;
    for (char& c : lower) c = tolower(c);

    size_t pos = lower.find("name");
    while (pos != std::string::npos)
    {
        size_t idx = pos + 4;
        while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n'))
        {
            idx++;
        }
        if (idx < lower.length() && lower[idx] == '(')
        {
            idx++;
            while (idx < lower.length() && (lower[idx] == ' ' || lower[idx] == '\t' || lower[idx] == '\r' || lower[idx] == '\n'))
            {
                idx++;
            }
            if (idx < lower.length() && lower[idx] == '"')
            {
                idx++;
                size_t startName = idx;
                size_t endName = ascii.find('"', startName);
                if (endName != std::string::npos)
                {
                    std::string nameStr = ascii.substr(startName, endName - startName);
                    return std::wstring(nameStr.begin(), nameStr.end());
                }
            }
        }
        pos = lower.find("name", pos + 1);
    }
    return L"";
}

