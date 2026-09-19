#include "ControlParser.h"

void ParseControlDetails(const std::wstring& cabview, const std::wstring& mass, std::wstring& outDetails)
{
    if (!cabview.empty() && !mass.empty()) {
        outDetails = L"Cab: " + cabview + L", " + mass + L" Mass";
    } else if (!mass.empty()) {
        outDetails = mass + L" Mass";
    } else {
        outDetails = L"Control Car";
    }
}
