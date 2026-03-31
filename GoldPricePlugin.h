#pragma once

#include "PluginInterface.h"
#include <windows.h>
#include <string>
#include <atomic>
#include <thread>

class CGoldPricePlugin : public ITMPlugin
{
public:
    static CGoldPricePlugin& GetInstance();

    virtual int GetAPIVersion() const override { return 7; }
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override {}
    virtual OptionReturn ShowOptionsDialog(void* hParent) override { return OR_OPTION_NOT_PROVIDED; }
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnMonitorInfo(const MonitorInfo& monitor_info) override {}
    virtual const wchar_t* GetTooltipInfo() override { return L""; }
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override {}

private:
    CGoldPricePlugin();
    ~CGoldPricePlugin();
    CGoldPricePlugin(const CGoldPricePlugin&) = delete;
    void operator=(const CGoldPricePlugin&) = delete;

    void WorkerThread();
    void FetchPrices();

    std::thread m_worker;
    std::atomic<bool> m_stop;

    // London gold data
    double m_londonPrice;
    double m_londonYesterday;
    std::wstring m_londonChangeAmt;   // ¸ÄÎª¿í×Ö·û´®
    std::wstring m_londonChangeRate;  // ¸ÄÎª¿í×Ö·û´®
    bool m_londonUp;

    // ZheShang gold data
    double m_zheShangPrice;
    double m_zheShangYesterday;
    std::wstring m_zheShangChangeAmt; // ¸ÄÎª¿í×Ö·û´®
    std::wstring m_zheShangChangeRate;// ¸ÄÎª¿í×Ö·û´®
    bool m_zheShangUp;

    CRITICAL_SECTION m_cs;

    class CLondonGoldItem : public IPluginItem
    {
    public:
        CLondonGoldItem(CGoldPricePlugin& plugin) : m_plugin(plugin) {}
        virtual const wchar_t* GetItemName() const override { return L"London Gold"; }
        virtual const wchar_t* GetItemId() const override { return L"LondonGold"; }
        virtual const wchar_t* GetItemLableText() const override { return L""; }
        virtual const wchar_t* GetItemValueText() const override { return L""; }
        virtual const wchar_t* GetItemValueSampleText() const override { return L"8888.88 ¡ü888.88(8.88%)"; }
        virtual bool IsCustomDraw() const override { return true; }
        virtual int GetItemWidthEx(void* hDC) const override;
        virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    private:
        CGoldPricePlugin& m_plugin;
    };

    class CZheShangGoldItem : public IPluginItem
    {
    public:
        CZheShangGoldItem(CGoldPricePlugin& plugin) : m_plugin(plugin) {}
        virtual const wchar_t* GetItemName() const override { return L"ZheShang Gold"; }
        virtual const wchar_t* GetItemId() const override { return L"ZheShangGold"; }
        virtual const wchar_t* GetItemLableText() const override { return L""; }
        virtual const wchar_t* GetItemValueText() const override { return L""; }
        virtual const wchar_t* GetItemValueSampleText() const override { return L"888.88 ¡ý-1.94(-0.19%)"; }
        virtual bool IsCustomDraw() const override { return true; }
        virtual int GetItemWidthEx(void* hDC) const override;
        virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    private:
        CGoldPricePlugin& m_plugin;
    };

    CLondonGoldItem* m_pLondonItem;
    CZheShangGoldItem* m_pZheShangItem;
};