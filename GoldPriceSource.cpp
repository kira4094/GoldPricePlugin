#include "pch.h"
#include "GoldPriceSource.h"
#include "third_party/nlohmann/json.hpp"
#include <wininet.h>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

#pragma comment(lib, "wininet.lib")

namespace GoldPriceSource
{
namespace
{
using Json = nlohmann::json;
struct InternetCloser { void operator()(void* handle) const { InternetCloseHandle(handle); } };
using InternetHandle = std::unique_ptr<void, InternetCloser>;

bool Number(const Json& object, const char* key, double& value)
{
    const auto& field = object.at(key);
    if (!field.is_number()) return false;
    value = field.get<double>();
    return std::isfinite(value);
}

bool Decimal(const std::string& text, double& value)
{
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = first + text.size();
    if (*first == '+') ++first;
    if (first == last || (*first == '-' && first != text.data())) return false;
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc() && result.ptr == last && std::isfinite(value);
}

std::wstring Signed(double value, bool percent)
{
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::showpos << std::fixed << std::setprecision(2) << value;
    if (percent) stream << L'%';
    return stream.str();
}

std::int64_t LondonTime(const Json& value)
{
    SYSTEMTIME time{};
    const auto component = [&value](const char* key, int minimum, int maximum, WORD& output)
    {
        const auto& field = value.at(key);
        if (!field.is_number_integer() || field < minimum || field > maximum) return false;
        output = field.get<WORD>();
        return true;
    };
    if (!component("year", 1970, 9999, time.wYear) || !component("monthValue", 1, 12, time.wMonth) ||
        !component("dayOfMonth", 1, 31, time.wDay) || !component("hour", 0, 23, time.wHour) ||
        !component("minute", 0, 59, time.wMinute) || !component("second", 0, 59, time.wSecond)) return 0;
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&time, &fileTime)) return 0;
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    // JD's zone-less tradeDateTime is interpreted as China Standard Time for diagnostics.
    return static_cast<std::int64_t>(ticks.QuadPart / 10000) - 11644473600000LL - 8 * 3600000LL;
}
}

std::string HttpGet(const wchar_t* url)
{
    InternetHandle session(InternetOpenW(L"GoldPricePlugin/0.0.1", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
    if (!session) return {};
    DWORD timeout = 3000;
    for (DWORD option : { INTERNET_OPTION_CONNECT_TIMEOUT, INTERNET_OPTION_SEND_TIMEOUT, INTERNET_OPTION_RECEIVE_TIMEOUT })
        if (!InternetSetOptionW(session.get(), option, &timeout, sizeof(timeout))) return {};

    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = 1;
    if (!InternetCrackUrlW(url, 0, 0, &parts)) return {};
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) return {};
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    InternetHandle connection(InternetConnectW(session.get(), host.c_str(), parts.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0));
    if (!connection) return {};
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_COOKIES;
    if (parts.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;
    InternetHandle request(HttpOpenRequestW(connection.get(), L"GET", path.c_str(), nullptr, nullptr, nullptr, flags, 0));
    if (!request) return {};
    const wchar_t headers[] = L"Accept: application/json\r\nCache-Control: no-cache\r\n";
    if (!HttpSendRequestW(request.get(), headers, _countof(headers) - 1, nullptr, 0)) return {};
    DWORD status = 0, statusSize = sizeof(status);
    if (!HttpQueryInfoW(request.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, nullptr) || status != 200) return {};

    std::string body;
    const ULONGLONG readStart = GetTickCount64();
    for (;;)
    {
        // Phase timeouts are 3 s; this additionally bounds a peer that keeps trickling data.
        if (GetTickCount64() - readStart >= 10000) return {};
        char buffer[4096];
        DWORD bytes = 0;
        if (!InternetReadFile(request.get(), buffer, sizeof(buffer), &bytes)) return {};
        if (!bytes) return body;
        if (body.size() + bytes > 256 * 1024) return {};
        body.append(buffer, bytes);
    }
}

bool ParseLondon(const std::string& body, Quote& quote)
{
    try
    {
        const auto root = Json::parse(body);
        if (root.at("success") != true || root.at("resultCode") != 0) return false;
        const auto& result = root.at("resultData");
        if (result.at("success") != true || result.at("code") != 200 || !result.at("data").is_array()) return false;
        for (const auto& item : result.at("data"))
        {
            if (item.value("uniqueCode", std::string()) != "WG-XAUUSD") continue;
            Quote next;
            double rate;
            if (!Number(item, "lastPrice", next.price) || !Number(item, "preClose", next.yesterday) ||
                !Number(item, "raise", next.change) || !Number(item, "raisePercent", rate) ||
                next.price <= 0 || next.yesterday <= 0 || !std::isfinite(rate * 100)) return false;
            next.changeAmount = Signed(next.change, false);
            next.changeRate = Signed(rate * 100, true);
            if (next.changeAmount.size() > 32 || next.changeRate.size() > 32) return false;
            next.sourceTimeMs = LondonTime(item.at("tradeDateTime"));
            if (next.sourceTimeMs <= 0) return false;
            quote = std::move(next);
            return true;
        }
    }
    catch (const Json::exception&) {}
    return false;
}

bool ParseZheShang(const std::string& body, Quote& quote)
{
    try
    {
        const auto root = Json::parse(body);
        if (root.at("success") != true || root.at("resultCode") != 0) return false;
        const auto& result = root.at("resultData");
        if (result.at("status") != "SUCCESS") return false;
        const auto& item = result.at("datas");
        if (item.at("productSku") != "1961543816") return false;
        Quote next;
        const std::string amount = item.at("upAndDownAmt").get<std::string>();
        const std::string rate = item.at("upAndDownRate").get<std::string>();
        double percent;
        if (!Decimal(item.at("price").get<std::string>(), next.price) ||
            !Decimal(item.at("yesterdayPrice").get<std::string>(), next.yesterday) ||
            !Decimal(amount, next.change) || rate.empty() || rate.back() != '%' ||
            !Decimal(rate.substr(0, rate.size() - 1), percent) || next.price <= 0 || next.yesterday <= 0 ||
            amount.size() > 32 || rate.size() > 32) return false;
        const std::string time = item.at("time").get<std::string>();
        const auto parsed = std::from_chars(time.data(), time.data() + time.size(), next.sourceTimeMs);
        if (parsed.ec != std::errc() || parsed.ptr != time.data() + time.size() || next.sourceTimeMs <= 0) return false;
        next.changeAmount.assign(amount.begin(), amount.end());
        next.changeRate.assign(rate.begin(), rate.end());
        quote = std::move(next);
        return true;
    }
    catch (const Json::exception&) {}
    return false;
}
}
