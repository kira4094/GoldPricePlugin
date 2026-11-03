#include <windows.h>
#include "PluginInterface.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "usage: gold_lifecycle_tests path/to/plugin.dll");
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            HMODULE module = LoadLibraryA(argv[1]); Check(module != nullptr, "load DLL");
            auto getInstance = reinterpret_cast<ITMPlugin* (*)()>(GetProcAddress(module, "TMPluginGetInstance"));
            Check(getInstance != nullptr, "exported entry point");
            ITMPlugin* plugin = getInstance(); Check(plugin && plugin->GetAPIVersion() == 7, "plugin API");
            plugin->DataRequired();
            const auto started = std::chrono::steady_clock::now();
            Check(FreeLibrary(module) != 0, "release host DLL reference");
            Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(200), "unload call must not wait for requests");
            Check(GetModuleHandleA(argv[1]) != nullptr, "in-flight callbacks retain DLL");
            while (GetModuleHandleA(argv[1]) && std::chrono::steady_clock::now() - started < std::chrono::seconds(10)) Sleep(10);
            Check(GetModuleHandleA(argv[1]) == nullptr, "DLL unloads after callbacks return");
        }
        std::cout << "PASS: load/export/unload during requests, 3 cycles\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
