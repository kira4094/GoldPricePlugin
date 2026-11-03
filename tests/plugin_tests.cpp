#include "GoldPricePlugin.h"
#include "fixtures.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
std::atomic<int> delays[2]{};
std::atomic<int> calls[2]{};
std::atomic<int> active[2]{};
std::atomic<int> peak[2]{};
std::atomic<bool> failures[2]{};
std::atomic<bool> throwOnce[2]{};
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

std::string GoldPriceTestHttpGet(bool london)
{
    const int index = london ? 0 : 1;
    const int running = ++active[index];
    if (running > peak[index]) peak[index] = running;
    ++calls[index];
    std::this_thread::sleep_for(std::chrono::milliseconds(delays[index].load()));
    --active[index];
    if (throwOnce[index].exchange(false)) throw std::runtime_error("simulated transport exception");
    if (failures[index]) return "{}";
    return london ? LondonFixture : ZheShangFixture;
}

struct GoldPricePluginTestAccess
{
    static double Price(CGoldPricePlugin& plugin, bool london)
    {
        EnterCriticalSection(&plugin.m_cs);
        const double value = london ? plugin.m_londonPrice : plugin.m_zheShangPrice;
        LeaveCriticalSection(&plugin.m_cs);
        return value;
    }
    static bool Idle(CGoldPricePlugin& plugin) { return !plugin.m_londonBusy && !plugin.m_zheShangBusy; }
};

int main()
{
    try
    {
        auto& plugin = CGoldPricePlugin::GetInstance();
        delays[0] = 1800;
        const auto start = std::chrono::steady_clock::now();
        plugin.DataRequired();
        for (int i = 0; i < 100; ++i) plugin.DataRequired();
        Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300), "DataRequired must not block on network");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        Check(GoldPricePluginTestAccess::Price(plugin, false) == 848.38, "ZheShang updates while London is delayed");
        Check(GoldPricePluginTestAccess::Price(plugin, true) == 0, "London fixture remains delayed");
        Check(calls[0] == 1 && calls[1] == 1, "no overlaps or premature requeue");
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        plugin.DataRequired();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check(calls[0] == 1 && calls[1] == 2, "fast source can poll again independently");
        while (!GoldPricePluginTestAccess::Idle(plugin)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(GoldPricePluginTestAccess::Price(plugin, true) == 3950.54, "delayed quote eventually publishes");

        failures[1] = true; delays[0] = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        plugin.DataRequired();
        while (!GoldPricePluginTestAccess::Idle(plugin)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(GoldPricePluginTestAccess::Price(plugin, false) == 848.38, "failed source preserves last good quote");
        Check(calls[0] == 2, "other source continues during business failure");
        Check(peak[0] == 1 && peak[1] == 1, "at most one request per source");

        failures[1] = false; delays[1] = 1600;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const int londonCalls = calls[0];
        plugin.DataRequired();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check(calls[0] == londonCalls + 1 && active[1] == 1, "London updates while ZheShang is delayed");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        plugin.DataRequired();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check(calls[0] == londonCalls + 2 && active[1] == 1, "London continues polling while ZheShang remains in flight");
        while (!GoldPricePluginTestAccess::Idle(plugin)) std::this_thread::sleep_for(std::chrono::milliseconds(10));

        delays[1] = 0; throwOnce[0] = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        plugin.DataRequired();
        while (!GoldPricePluginTestAccess::Idle(plugin)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const int afterException = calls[0];
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        plugin.DataRequired();
        while (!GoldPricePluginTestAccess::Idle(plugin)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(calls[0] == afterException + 1, "exception releases busy flag and next request can run");
        Check(peak[0] == 1 && peak[1] == 1, "no overlap after delays or exceptions");
        Check(plugin.GetAPIVersion() == 7 && plugin.GetItem(0) && plugin.GetItem(1) && !plugin.GetItem(2), "plugin interface preserved");
        std::cout << "PASS: independent scheduling, no overlap, last-good retention\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
