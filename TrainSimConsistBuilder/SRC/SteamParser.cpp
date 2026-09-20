#include "SteamParser.h"

void ParseSteamDetails(const std::wstring& power, const std::wstring& mass, std::wstring& outDetails)
{
    if (!power.empty() && !mass.empty()) {
        outDetails = power + L", " + mass + L" Mass";
    } else if (!power.empty()) {
        outDetails = power;
    } else if (!mass.empty()) {
        outDetails = mass + L" Mass";
    } else {
        outDetails = L"Steam Locomotive";
    }
}
