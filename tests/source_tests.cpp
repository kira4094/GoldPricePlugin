#include "GoldPriceSource.h"
#include "third_party/nlohmann/json.hpp"
#include "fixtures.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

static void ParserTests()
{
    GoldPriceSource::Quote quote;
    Check(GoldPriceSource::ParseLondon(LondonFixture, quote), "valid London quote");
    Check(quote.price == 3950.54 && quote.yesterday == 3946 && quote.changeAmount == L"+4.54" && quote.changeRate == L"+0.12%", "London values and percentage units");
    Check(quote.sourceTimeMs == 1793666811000LL, "London timestamp uses UTC+8");
    Json london = Json::parse(LondonFixture);
    auto& item = london["resultData"]["data"][0];
    item["raise"] = -4.54; item["raisePercent"] = -0.0011505;
    Check(GoldPriceSource::ParseLondon(london.dump(2), quote) && quote.changeAmount == L"-4.54" && quote.changeRate == L"-0.12%", "falling and reordered/spaced JSON");
    london["resultData"]["data"].insert(london["resultData"]["data"].begin(), Json{{"uniqueCode", "OTHER"}, {"lastPrice", 1}});
    Check(GoldPriceSource::ParseLondon(london.dump(), quote) && quote.price == 3950.54, "select exact symbol");
    const auto good = Json::parse(LondonFixture);
    for (const auto& field : {"lastPrice", "preClose", "raise", "raisePercent", "tradeDateTime"})
    {
        auto invalid = good; invalid["resultData"]["data"][0].erase(field);
        quote.price = 123;
        Check(!GoldPriceSource::ParseLondon(invalid.dump(), quote) && quote.price == 123, "missing fields preserve prior quote");
    }
    for (const Json badValue : {Json("3950.54"), Json(nullptr), Json(0), Json(-1)})
    {
        auto invalid = good; invalid["resultData"]["data"][0]["lastPrice"] = badValue;
        Check(!GoldPriceSource::ParseLondon(invalid.dump(), quote), "invalid London price types and values");
    }
    auto invalid = good; invalid["success"] = false;
    Check(!GoldPriceSource::ParseLondon(invalid.dump(), quote), "outer business failure");
    invalid = good; invalid["resultCode"] = 1;
    Check(!GoldPriceSource::ParseLondon(invalid.dump(), quote), "outer error code");
    invalid = good; invalid["resultData"]["code"] = 500;
    Check(!GoldPriceSource::ParseLondon(invalid.dump(), quote), "inner business failure");
    invalid = good; invalid["resultData"]["data"][0]["uniqueCode"] = "OTHER";
    Check(!GoldPriceSource::ParseLondon(invalid.dump(), quote), "wrong symbol");
    for (const auto& body : {std::string(""), std::string("<html>error</html>"), LondonFixture.substr(0, LondonFixture.size() - 1), std::string("{\"lastPrice\":NaN}")})
        Check(!GoldPriceSource::ParseLondon(body, quote), "invalid or truncated JSON");
    auto huge = LondonFixture; huge.replace(huge.find("3950.54"), 7, "1e999");
    Check(!GoldPriceSource::ParseLondon(huge, quote), "nonfinite London value");
    for (const Json year : {Json(2026.5), Json(67562), Json(-1)})
    {
        auto wrongTime = good; wrongTime["resultData"]["data"][0]["tradeDateTime"]["year"] = year;
        Check(!GoldPriceSource::ParseLondon(wrongTime.dump(), quote), "reject fractional or overflowing timestamp components");
    }

    Check(GoldPriceSource::ParseZheShang(ZheShangFixture, quote), "valid ZheShang quote");
    Check(quote.price == 848.38 && quote.changeAmount == L"+1.11" && quote.changeRate == L"+0.13%", "preserve ZheShang formatting");
    const auto zhe = Json::parse(ZheShangFixture);
    for (const auto& text : {"848.38junk", "NaN", "inf", "", "0", "-1", "+-1", "++1"})
    {
        auto bad = zhe; bad["resultData"]["datas"]["price"] = text;
        quote.price = 123;
        Check(!GoldPriceSource::ParseZheShang(bad.dump(), quote) && quote.price == 123, "strict ZheShang numeric strings");
    }
    auto bad = zhe; bad["resultData"]["datas"].erase("upAndDownRate");
    Check(!GoldPriceSource::ParseZheShang(bad.dump(), quote), "missing ZheShang change");
    std::cout << "PASS: quote parser regression cases\n";
}

static std::int64_t WallTimeMs()
{
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{}; value.LowPart = time.dwLowDateTime; value.HighPart = time.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart / 10000) - 11644473600000LL;
}

static double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(percentile * values.size())) - 1];
}

static void Sample(bool london, int seconds, std::ofstream& output, std::mutex& mutex)
{
    const auto start = Clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto lastUpdate = start;
    std::int64_t lastSourceTime = 0;
    double lastPrice = 0;
    std::vector<double> durations, ages;
    int successes = 0, failures = 0, consecutiveFailures = 0, maxConsecutiveFailures = 0, updates = 0, priceChanges = 0;
    long long longestStaleMs = 0;
    const char* source = london ? "london" : "zheshang";
    while (Clock::now() < end)
    {
        const auto sent = Clock::now();
        GoldPriceSource::Quote quote;
        const auto body = GoldPriceSource::HttpGet(london ? GoldPriceSource::LondonUrl : GoldPriceSource::ZheShangUrl);
        const bool ok = london ? GoldPriceSource::ParseLondon(body, quote) : GoldPriceSource::ParseZheShang(body, quote);
        const auto received = Clock::now();
        const auto receivedMs = WallTimeMs();
        const double elapsed = std::chrono::duration<double, std::milli>(received - sent).count();
        durations.push_back(elapsed);
        Json record{{"kind", "sample"}, {"source", source}, {"ok", ok}, {"received_epoch_ms", receivedMs}, {"request_ms", elapsed}};
        if (ok)
        {
            ++successes; consecutiveFailures = 0;
            const double age = static_cast<double>(receivedMs - quote.sourceTimeMs);
            ages.push_back(age);
            if (quote.sourceTimeMs != lastSourceTime) { ++updates; lastUpdate = received; }
            if (lastSourceTime && quote.price != lastPrice) ++priceChanges;
            lastSourceTime = quote.sourceTimeMs; lastPrice = quote.price;
            record["price"] = quote.price; record["source_epoch_ms"] = quote.sourceTimeMs; record["quote_age_ms"] = age;
        }
        else { ++failures; maxConsecutiveFailures = (std::max)(maxConsecutiveFailures, ++consecutiveFailures); }
        const auto stale = std::chrono::duration_cast<std::chrono::milliseconds>(received - lastUpdate).count();
        longestStaleMs = (std::max)(longestStaleMs, stale);
        record["source_unchanged_ms"] = stale;
        { std::lock_guard<std::mutex> lock(mutex); output << record.dump() << '\n'; output.flush(); }
        std::this_thread::sleep_until(sent + std::chrono::seconds(1));
    }
    Json summary{{"kind", "summary"}, {"source", source}, {"successes", successes}, {"failures", failures}, {"samples", successes + failures},
        {"duration_seconds", std::chrono::duration<double>(Clock::now() - start).count()}, {"request_p50_ms", Percentile(durations, .5)},
        {"request_p95_ms", Percentile(durations, .95)}, {"request_max_ms", durations.empty() ? 0 : *std::max_element(durations.begin(), durations.end())},
        {"quote_age_p50_ms", Percentile(ages, .5)}, {"quote_age_p95_ms", Percentile(ages, .95)},
        {"quote_age_max_ms", ages.empty() ? 0 : *std::max_element(ages.begin(), ages.end())}, {"source_timestamp_updates", updates},
        {"price_changes", priceChanges}, {"longest_source_unchanged_ms", longestStaleMs}, {"max_consecutive_failures", maxConsecutiveFailures}};
    std::lock_guard<std::mutex> lock(mutex);
    output << summary.dump() << '\n'; output.flush();
    std::cout << summary.dump() << '\n';
}

int main(int argc, char** argv)
{
    try
    {
        ParserTests();
        if (argc > 1 && std::string(argv[1]) == "--live")
        {
            Check(argc == 4, "usage: gold_source_tests --live SECONDS output.jsonl");
            const int seconds = std::stoi(argv[2]); Check(seconds > 0, "positive duration required");
            std::ofstream output(argv[3]); Check(output.good(), "cannot create output file");
            output << Json{{"kind", "metadata"}, {"started_epoch_ms", WallTimeMs()}, {"london_timestamp_zone_assumption", "UTC+08:00"}, {"cadence_ms", 1000}}.dump() << '\n';
            std::mutex mutex;
            std::thread london(Sample, true, seconds, std::ref(output), std::ref(mutex));
            std::thread zhe(Sample, false, seconds, std::ref(output), std::ref(mutex));
            london.join(); zhe.join();
        }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
