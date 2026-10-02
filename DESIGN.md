# VolumeEdit 设计 · 总音量模式

## 目标

用户选择直接修改默认输出设备的总音量。Windows 原生 C++20 GUI，MSVC 静态 CRT，ZIP 解压运行。统一图标、托盘、开机启动、0 dB 特殊标记、音量复位；无静音按钮或预设。正常运行不创建驱动、服务、安装向导、计划任务、HKLM 项或全局 PATH 修改。

Makefile VERSION = 0.1 是版本唯一来源，显示 v0.1，PE/manifest 0.1.0.0，配置 schema 独立。已有 main、releases/v0.1 和 v0.1 tag 不因 dev/init 开发变化移动。

## 总音量接口与语义

通过 IAudioEndpointVolume 的 GetVolumeRange、GetMasterVolumeLevel、SetMasterVolumeLevel 查询范围与直接设置输出总音量。Windows 系统滑块同步变化。正常控制不枚举应用、查询应用令牌、修改会话音量或静音标志。

界面 −40～+40 dB 表示相对启用时基准的偏移：

target = clamp(baseline_dB + offset_dB, device_minimum_dB, device_maximum_dB)

0.1 dB 是输入精度；设备可量化到最近硬件档，接口读回值可能仍是请求值。不能突破硬件最小值，也不能实现独立的混音后额外增益。当前 C-Media 为 −45～0 dB、1 dB 步进。设备实际 0 dB 与界面基准 0 dB 区分显示，越界提示限制。

## 工作线程

主线程负责 UI、输入、托盘和配置。MTA COM 线程约每 100 ms 检查输出与总音量，默认使用 eConsole 默认输出，允许手动指定；断开进入等待状态。互斥锁一致复制设置与状态，修订号防止旧配置覆盖新输入，事件通知重试/停止。

首次接管设备捕获基准，每次目标都由基准计算，不累积偏移。0 dB 和退出恢复基准；切换前尝试恢复旧设备，失败保留记录。崩溃重启匹配上次应用值则沿用旧基准，避免把已降低的音量作为原值。

Windows 音量键或滑块的外部 dB 更改优先。发现当前值不匹配本工具最后写入值，放弃对应备份，将当前值作为新基准，偏移置 0，保存配置，退出不覆盖它。采用有限浮点容差，读写没有系统原子事务，极短并发变化仍有竞态。

正常退出保留不可恢复任务，不再弹通用模态提示；下一次运行显示状态并重试。完整卸载仍要求恢复资源后清理依据。

## 数据与事务

持久数据只在 EXE 同目录 state。

| 文件 | 内容 |
| --- | --- |
| settings.json | schema v4、总音量偏移及路由 |
| endpoint-volumes.txt | 每设备原始 dB、计划/已应用 dB、写入前值 |
| volumes.txt | 旧版会话恢复任务 |
| instance.id | 部署 GUID |
| startup.txt | 当前及旧启动命令所有权 |
| helper.txt | 旧服务追踪，仅升级清理 |

VE_ENDPOINT_BACKUP_1 使用严格 UTF-8 十六进制端点 ID、有限浮点 dB 和可缺省的写入前值。限制长度、条数，拒绝重复键及格式错误。配置 v1/v2/v3 合法设备选择保留，但旧会话偏移迁移为 0；非法旧配置不猜测执行。

先原子保存原值、目标、写入前值，再写总音量，成功后读回并保存应用值。崩溃窗口匹配目标或写入前值均可恢复。写入失败回滚先前备份。恢复仅在当前值匹配时回写，外部更改优先，不可访问设备保留记录，损坏依据阻止写入及卸载。

原子文件采用同目录 .tmp、FlushFileBuffers、MoveFileEx；每级路径拒绝重解析点。卸载包含已知 .tmp，保留未知文件。

## 旧会话迁移与服务退出

legacy_recovery.cpp 仅在旧 volumes.txt 存在时进行兼容恢复，约每 3 秒及退出重试。仍验证用户归属和实例身份，只恢复匹配的值，多实例歧义保留。关闭应用的会话可能消失，而 Windows 可保留音量，不能直接丢弃记录。

跨部署只在同设备、同稳定身份、各自唯一候选、旧 applied 与新 original 一致且无未完成事务时确认衰减链，将新 original 提升到最早基准。重复同实例且内容冲突则拒绝；其他歧义保留。先备份源文件，先持久化合并任务，再删除来源重复任务，禁止无依据重置全部应用。

2026-10-02 本机合并 out 和旧解压目录记录。Chrome、游戏、Riot 已恢复及核对；PowerShell 通过隐藏零 PCM 流重建可访问会话后处理。剩余 WSL msrdc 会话不存在，继续保留。迁移工具仅在 build，旧数据备份只在 out/reports，不提交或发布。

旧辅助程序保留作兼容恢复/清理。客户端删除启用/更新服务入口，开机启动不再配置服务；有 helper.txt 时才显示“清理旧版辅助服务”。UAC 后验证 GUID、SID、来源目录、SCM 路径、Program Files ACL，停止服务、等待进程退出、删除并确认 SCM 对象消失，再清理固定文件及空目录。清理可由同目录 VolumeEdit.exe 或 uninstall.exe 发起；正常运行不注册服务。

## 启动与卸载

开机启动仅写 HKCU Run 的部署 GUID 专属值，命令为当前 VolumeEdit.exe --tray。startup.txt 追踪移动前后的命令，更新/删除验证归属，不触碰同名但不属于本部署的值。Local\\VolumeEdit.Portable.v3 互斥对象保持与旧版的单实例兼容。

卸载阻止重启，按窗口类和完整进程路径只关闭本目录客户端，恢复总音量和旧会话，清理旧服务及启动项，删除已知 state 和产品文件，最后等待自身退出清理 EXE 与 product.id。有未恢复任务则保留程序和依据，不报告完成。

最后清理使用系统 PowerShell 隐藏进程，脚本仅在子进程命令中，无临时脚本、持久任务或注册表延迟删除项；检查父路径、重解析点及产品标记，只删除固定清单，保留用户文件和报告，空目录才删除。系统产生的缓存/日志不属于本产品清理范围。

## 构建、产物与检查

CMake 读取 Makefile 版本并生成 PE 资源和 manifest。NMake/GNU Make 调用同一 PowerShell 脚本；默认三个 EXE 直接输出到 out，备用输出仅在 out 内。客户端/卸载/旧版兼容程序均静态 CRT、asInvoker、PerMonitorV2、DEP、ASLR、CFG、统一内嵌图标。

ZIP 严格清单：三个 EXE、product.id、README、DESIGN、BUILD_STATUS、LICENSE、SHA256SUMS，共 9 文件；排除 state、reports、PDB、测试/诊断工具及未知文件。自动报告放 out/reports，BUILD_STATUS 是人工维护的验证边界。

11 项 CTest 覆盖配置迁移、默认路由、衰减链合并、总音量边界、严格事务备份、旧权限兼容、真实管道、GUI、产品清理和自删除，不修改真实音量或启动项。独立实机检查在临时静音及临时部署中验证总音量写入、复位、退出、工作线程重启和外部改动优先，结束恢复原总音量/静音。系统重启、热插拔、不同驱动与文件锁仍需验收。
