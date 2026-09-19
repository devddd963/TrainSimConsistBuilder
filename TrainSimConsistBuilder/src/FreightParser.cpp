#include "FreightParser.h"

void ParseFreightDetails(const std::wstring& mass, std::wstring& outDetails)
{
    if (!mass.empty()) {
        outDetails = L"Freight Car, " + mass + L" Mass";
    } else {
        outDetails = L"Freight Car";
    }
}
