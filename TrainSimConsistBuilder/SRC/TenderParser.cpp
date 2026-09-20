#include "TenderParser.h"

void ParseTenderDetails(const std::wstring& mass, std::wstring& outDetails)
{
    if (!mass.empty()) {
        outDetails = L"Tender, " + mass + L" Mass";
    } else {
        outDetails = L"Tender";
    }
}
