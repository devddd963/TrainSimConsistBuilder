#include "ConsistWriter.h"
#include <sstream>
#include <fstream>
#include <iomanip>

namespace ConsistWriter
{
    bool SaveConsist(const std::wstring& fullPath,
                     const std::wstring& trainCfgId,
                     const std::wstring& name,
                     double maxVelocityKmh,
                     double perfFactorPct,
                     const std::vector<ConsistReader::UnitInfo>& units)
    {
        // Clean strings (trim spaces/quotes)
        auto TrimQuotes = [](const std::wstring& s) -> std::wstring {
            if (s.empty()) return L"";
            size_t start = 0;
            while (start < s.size() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'"')) {
                start++;
            }
            size_t end = s.size();
            while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'"')) {
                end--;
            }
            return s.substr(start, end - start);
        };

        std::wstring cleanCfgId = TrimQuotes(trainCfgId);
        std::wstring cleanName  = TrimQuotes(name);

        double maxVelocityMs = maxVelocityKmh / 3.6;
        double perfFraction  = perfFactorPct / 100.0;

        std::wstringstream wss;
        wss << L"SIMISA@@@@@@@@@@JINX0D0t______\r\n\r\n";
        wss << L"Train (\r\n";
        wss << L"\tTrainCfg ( \"" << cleanCfgId << L"\"\r\n";
        wss << L"\t\tName ( \"";
        if (!cleanName.empty()) {
            wss << cleanName;
        } else {
            wss << cleanCfgId;
        }
        wss << L"\" )\r\n";
        wss << L"\t\tSerial ( 1 )\r\n";

        wchar_t velBuf[64], perfBuf[64];
        swprintf_s(velBuf, 64, L"%.5f", maxVelocityMs);
        swprintf_s(perfBuf, 64, L"%.5f", perfFraction);
        wss << L"\t\tMaxVelocity ( " << velBuf << L" " << perfBuf << L" )\r\n";
        wss << L"\t\tNextWagonUID ( " << units.size() << L" )\r\n";
        wss << L"\t\tDurability ( 1.00000 )\r\n";

        for (size_t i = 0; i < units.size(); i++)
        {
            const auto& unit = units[i];
            std::wstring unitType = unit.isEngine ? L"Engine" : L"Wagon";
            std::wstring dataType = unit.isEngine ? L"EngineData" : L"WagonData";

            std::wstring cleanUnitId = TrimQuotes(unit.uid);
            std::wstring cleanParent = TrimQuotes(unit.parentDir);

            wss << L"\t\t" << unitType << L" (\r\n";
            wss << L"\t\t\t" << dataType << L" ( \"" << cleanUnitId << L"\" \"" << cleanParent << L"\" )\r\n";
            wss << L"\t\t\tUiD ( " << i << L" )\r\n";
            if (unit.isFlipped)
            {
                wss << L"\t\t\tFlip ( )\r\n";
            }
            wss << L"\t\t)\r\n";
        }

        wss << L"\t)\r\n";
        wss << L")\r\n";

        std::wstring content = wss.str();

        FILE* fp = nullptr;
        if (_wfopen_s(&fp, fullPath.c_str(), L"wb") == 0 && fp != nullptr)
        {
            unsigned short bom = 0xFEFF;
            fwrite(&bom, sizeof(bom), 1, fp);
            fwrite(content.c_str(), sizeof(wchar_t), content.size(), fp);
            fclose(fp);
            return true;
        }
        return false;
    }
}
