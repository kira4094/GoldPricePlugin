<!-- 语言切换链接 -->
<p align="center">
  <a href="#中文">中文</a> | 
  <a href="#english">English</a>
</p>

---

<a id="中文"></a>
## 📈 黄金价格插件 for TrafficMonitor

为 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 开发的插件，在任务栏实时显示**伦敦金**和**浙商银行积存金**价格，涨跌自动变色（红涨绿跌），每 1 秒刷新一次。

### ✨ 功能特点
- 伦敦金价格（美元/盎司） + 涨跌额 + 涨跌幅  
- 浙商金价格（元/克） + 涨跌额 + 涨跌幅  
- 显示格式：`当前价 ↑/↓涨跌额(涨跌幅)`  
- 颜色：红涨绿跌（深色模式下柔和配色）  
- 自动跟随 TrafficMonitor 字体和主题  
- 使用 Windows 原生 WinINet，无额外依赖

### 📸 效果预览
<p align="center">
  <img src="Image/GoldPricePlugin.png" alt="插件效果图" width="600">
</p>

### 📥 安装
1. 从 [Releases](../../releases) 下载 `GoldPricePlugin.dll`  
2. 将 DLL 放入 TrafficMonitor 安装目录下的 `plugins` 文件夹（若不存在则新建）  
3. 重启 TrafficMonitor  
4. 右键任务栏图标 → **其他功能** → **插件管理**，确认插件已加载  
5. 在 **显示项目** 中勾选 **London Gold** 和 **ZheShang Gold**

### 🔧 自行编译
- 需要 Visual Studio 2022（含 C++ 桌面开发）  
- 打开 `GoldPricePlugin.sln`，选择 **Release** 和 **x64**（或与你的 TrafficMonitor 相同架构）  
- 生成 → 生成解决方案  
- 生成的 `GoldPricePlugin.dll` 位于 `x64\Release\`

### 📊 数据来源
- **伦敦金**：新浪财经
- **浙商金**：京东黄金

### ⚠️ 注意事项
- 如果价格不显示，请检查网络连接和防火墙是否允许 TrafficMonitor 联网  
- 插件刷新间隔为 2 秒，免费 API 有频率限制，但此频率在安全范围内  
- 深色模式下红绿颜色会略微调柔，保证可读性

### 🙏 致谢
本插件由 [DeepSeek](https://deepseek.com) 辅助创建。

### 📄 开源协议
MIT License

---

<a id="english"></a>
## 📈 Gold Price Plugin for TrafficMonitor

A plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) that displays real-time **London Gold** and **ZheShang Gold** prices on the taskbar. Prices are shown with change amount and percentage, color-coded (red for up, green for down). Refreshes every 2 seconds.

### ✨ Features
- London Gold price (USD/oz) + change amount + change percentage  
- ZheShang Gold price (CNY/g) + change amount + change percentage  
- Format: `current price ↑/↓change amount(change percentage)`  
- Colors: red for increase, green for decrease (softer in dark mode)  
- Follows TrafficMonitor’s font and theme  
- Uses native Windows WinINet API, no extra dependencies

### 📸 Screenshot
<p align="center">
  <img src="Image/GoldPricePlugin.png" alt="Plugin screenshot" width="600">
</p>

### 📥 Installation
1. Download `GoldPricePlugin.dll` from [Releases](../../releases)  
2. Place it into the `plugins` folder inside your TrafficMonitor installation directory (create if not exists)  
3. Restart TrafficMonitor  
4. Right-click the tray icon → **Other Features** → **Plugin Manager**, confirm the plugin is loaded  
5. Go to **Display Items** and check **London Gold** and **ZheShang Gold**

### 🔧 Build from Source
- Requires Visual Studio 2022 (with “Desktop development with C++”)  
- Open `GoldPricePlugin.sln`, set configuration to **Release**, platform to **x64** (or match your TrafficMonitor)  
- Build → Build Solution  
- The resulting `GoldPricePlugin.dll` is in `x64\Release\`

### 📊 Data Sources
- **London Gold**: Sina Finance
- **ZheShang Gold**: JD.com Gold

### ⚠️ Notes
- If prices do not appear, check your internet connection and firewall permissions for TrafficMonitor  
- Refresh interval is 2 seconds; free APIs may have rate limits, but this frequency is generally safe  
- In dark mode, colors are slightly softened for better readability

### 🙏 Acknowledgement
This plugin was created with the assistance of [DeepSeek](https://deepseek.com).

### 📄 License
MIT License
