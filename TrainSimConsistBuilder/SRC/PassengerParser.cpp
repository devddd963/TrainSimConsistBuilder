#include "PassengerParser.h"

void ParsePassengerDetails(const std::wstring& mass, std::wstring& outDetails)
{
    if (!mass.empty()) {
        outDetails = L"Passenger Coach, " + mass + L" Mass";
    } else {
        outDetails = L"Passenger Coach";
    }
}
