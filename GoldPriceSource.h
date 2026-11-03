#pragma once

#include <cstdint>
#include <string>

namespace GoldPriceSource
{
inline constexpr wchar_t LondonUrl[] = L"https://ms.jr.jd.com/gw2/generic/jdtwt/h5/m/getSimpleQuoteUseUniqueCodes?reqData=%7B%22ticket%22%3A%22gold-price-h5%22%2C%22uniqueCodes%22%3A%5B%22WG-XAUUSD%22%5D%7D";
inline constexpr wchar_t ZheShangUrl[] = L"https://api.jdjygold.com/gw2/generic/jrm/h5/m/stdLatestPrice?productSku=1961543816";

struct Quote
{
    double price = 0;
    double yesterday = 0;
    double change = 0;
    std::wstring changeAmount;
    std::wstring changeRate;
    std::int64_t sourceTimeMs = 0;
};

std::string HttpGet(const wchar_t* url);
bool ParseLondon(const std::string& body, Quote& quote);
bool ParseZheShang(const std::string& body, Quote& quote);
}
