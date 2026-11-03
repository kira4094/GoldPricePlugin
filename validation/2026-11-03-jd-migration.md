# 京东伦敦金切换与独立请求验证

日期：2026-11-03；执行主机：KWOK_WORK_PC；时区：Asia/Shanghai。

## 已接受的修改

- 伦敦金改为京东金融 `getSimpleQuoteUseUniqueCodes`，固定品种 `WG-XAUUSD`。
- 浙商金继续使用 `stdLatestPrice`、SKU `1961543816`。
- `DataRequired` 分别排队两个独立请求；每源至少 1 秒启动间隔、最多一个在途任务，成功后独立发布行情。
- 使用线程池和在途 DLL 引用，移除常驻线程及析构中的 `join`。
- HTTP 状态、完整读取、JSON 业务状态、品种及数值验证失败时保留上一份有效报价。
- WinINet 连接/发送/接收阶段各设 3 秒超时，禁止缓存，并限制读取周期和响应大小。这不是严格的整体请求 3 秒超时。
- 保留 API 7、显示项 ID、显示格式和版本号。

## 构建和自动测试

| 检查 | 结果 |
|---|---|
| CMake x64 Release | PASS |
| CMake Win32 Release | PASS |
| 原 GoldPricePlugin.vcxproj x64 Release | PASS |
| quote_parsers：正常、降价、乱序字段、错误品种、业务失败、缺字段、错误数值和日期、损坏 JSON | PASS，0.01 秒 |
| independent_requests：双向延迟隔离、同源不重叠、坏响应保留旧价、异常后恢复 | PASS，7.16 秒 |
| unload_during_request：加载测试 DLL、在途请求期间释放、最终卸载，3 个周期 | PASS，3.68 秒 |
| 对实际 x64 DLL 再运行加载/导出/API/在途请求卸载，3 个周期 | PASS，退出码 0 |
| 独立只读代码审查 | PASS，无阻断问题 |
| 本次修改文件 git diff --check | PASS |

本地 CTest 日志（未纳入仓库）：`../build/jd-verify-tests/Testing/Temporary/LastTest.log`。

## 5 分钟联网采样

命令（项目根目录）：

```powershell
& .\build\jd-verify-tests\Release\gold_source_tests.exe --live 300 .\build\jd-verify-tests\jd-stability-20261103.jsonl
```

北京时间约 **08:58:01—09:03:01**。两路线程分别按约 1 秒节奏采样，直接复用生产 `GoldPriceSource::HttpGet` 和解析器。有效成功必须同时通过 HTTP、业务状态和行情解析校验。

| 指标 | 伦敦金 | 浙商金 |
|---|---:|---:|
| 有效响应 / 请求次数 | 298 / 298 | 298 / 298 |
| 请求或解析失败 | 0 | 0 |
| 请求耗时中位数 | 46.59 ms | 42.04 ms |
| 请求耗时 P95 | 58.27 ms | 50.74 ms |
| 请求耗时最大值 | 219.18 ms | 135.74 ms |
| 报价时间距收到时间，中位数 | 2.668 s | 3.081 s |
| 报价时间距收到时间，P95 | 3.231 s | 3.636 s |
| 报价时间距收到时间，最大值 | 5.121 s | 4.110 s |
| 观察到价格变化次数（不含首值） | 287 | 250 |
| 观察到新的源时间戳次数（含首值） | 277 | 269 |
| 连续观测到不同源时间戳的最大间隔 | 3.019 s | 2.029 s |
| 源时间戳倒退次数 | 0 | 0 |

本地原始逐次记录和汇总（未纳入仓库）：`../build/jd-verify-tests/jd-stability-20261103.jsonl`。

伦敦金 `tradeDateTime` 未带时区，本报告按 UTC+08:00 解释；浙商使用接口 epoch 毫秒字段。报价时间差包含源时钟与本机时钟误差，不等于经过独立权威行情对齐后的市场延迟。最大更新时间戳间隔由原始记录重新计算，区别于采样程序的 `longest_source_unchanged_ms`（截至当次采样尚未变化的持续时间）。

本轮结果支持：两路在这 5 分钟内均能持续成功获取，测试注入的慢请求不会拖住另一项。没有与新浪做同期对照，因此不宣称京东在所有时段都比新浪更快或更稳定。

## 构建产物

- x64：`../build/jd-verify/Release/GoldPricePlugin.dll`
  - SHA-256：`17641ffdbfdb9b92873d067631ce925618e85ce98ad81331adc856b37a32070e`
- Win32：`../build/jd-verify-win32/Release/GoldPricePlugin.dll`
  - SHA-256：`416a3ac9554b2b6eed454553443e02c7a563bdd90005139581f702251e8063ed`

## 验证边界

- 后续用户已确认 x64 版在 TrafficMonitor 中成功启动；此前错误 193 来自误用插件架构。该启动结果由用户反馈，未独立验证任务栏实际绘制、字体、布局或长时间运行。
- 没有进行全天、多网络或交易高峰的持续稳定性验证；京东网页接口没有已确认的第三方长期免费/SLA 承诺。
- 获取失败仍显示上次有效价格，未新增过期标记；初次成功前仍显示原有初始值。
- 原有文本宽度计算问题不在本次修改范围，未调整绘制逻辑。

Coordinator 最终验收：本次授权的接口替换、请求隔离、构建和短时稳定性验证已完成；保留上述运行及长期稳定性边界。
