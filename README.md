# VolumeEdit v0.1 · 总音量微调

<p><img src="assets/volumeedit.png" alt="VolumeEdit 图标" width="96"></p>

[Apache-2.0 许可证](LICENSE) · [设计文档](DESIGN.md) · [开发构建](DEVELOPMENT.md) · [验证记录](BUILD_STATUS.md)

Windows 原生 C++ 图形客户端，直接按 dB 调节当前默认输出设备的 **Windows 总音量**。解压运行，无驱动、无安装向导，正常使用不注册服务、不需要管理员权限。统一耳机图标、托盘和单一开机启动开关。

## 使用

1. 完整解压 VolumeEdit-portable-v0.1-x64.zip 到自己可写的独立目录。
2. 双击 **VolumeEdit.exe**。首次运行固定在 **0.0 dB**，表示相对于启用时总音量的偏移。蓝色文字和中央标记表示基准；设备实际 0 dB 通常代表最大音量，两者不要混淆。
3. 输出默认跟随 Windows 当前默认输出设备，无需手动选择；也可指定设备。
4. 拖动滑块或输入 **−40～+40 dB** 后按回车，**音量复位**返回基准。Windows 总音量滑块同步变化；界面显示设备实际 dB、基准、范围和步进。
5. 勾选 **开机启动**，下次当前用户登录后自动运行到托盘，并加载保存配置。关闭窗口继续运行；托盘右键“退出并恢复音量”停止控制并尝试恢复设备基准。

硬件只接受自身范围和步进。当前测试 C-Media 输出为 **−45～0 dB、1 dB 步进**。越界目标限制到边界并提示；0.1 dB 输入精度不代表硬件精度。达到最低值后仍太响，本接口不能继续增加衰减，也不提供超过设备最大值的混音后增益。

手动使用 Windows 音量滑块或音量键时，程序尊重更改，将当前总音量作为新的 0 dB 基准并复位偏移；退出不会覆盖这次更改。程序没有静音按钮，也不更改 Windows 静音标志。

设置和恢复依据保存在本目录 state。更新前从托盘退出旧版，覆盖产品文件并保留 state；移动后切换一次开机启动以更新路径。不要轮流使用多个各有状态的旧副本。

## 旧版影响与升级

旧版修改应用会话音量。新版读取旧配置时保留设备选择，将旧会话衰减值迁移为 **0 dB 总音量偏移**，避免混用含义。旧 volumes.txt 仅用于兼容恢复，新控制不再逐个修改应用会话。

有备份且相关音频会话可访问时，尝试恢复原会话音量。用户或应用已经另行修改的值保持不动。关闭应用、断开的设备或有多实例歧义的记录保留，应用重新发声后自动重试；界面显示“旧版待恢复”数量，**退出不再因历史任务反复弹窗**。不能将保留任务描述为已全部恢复。

以前试用过服务的部署会显示 **清理旧版辅助服务**。点击并按 UAC 确认，可删除该部署的服务及受保护文件。新版总音量控制不需要服务；清理旧系统服务仍须管理员授权。

恢复旧会话音量可能让声音变大，建议先降低总音量。不要直接删除 volumes.txt，以免丢失恢复依据。“重置所有应用音量”还会改变其他应用的自定义音量，不能作为针对本产品的恢复。

## 卸载

双击 **uninstall.exe**，点击“卸载”。它关闭本目录客户端，恢复记录中的设备总音量与旧会话音量，清理本部署的启动项、状态和产品文件；如仍有旧服务，按管理员提示完成清理。

原设备离线或旧会话尚不可恢复时，卸载提示未完成并保留记录和程序。正常退出安静保留任务，完整卸载则要求先处理待恢复项，避免丢失依据后声称无残留。

自身 EXE 由系统 PowerShell 隐藏进程在退出后清理，不生成临时脚本、持久任务或注册表延迟删除项。自建文件、ZIP、源码和报告保留，空产品目录才删除。PowerShell 被禁用、文件占用或权限异常时请保留卸载程序重试。本产品不清理 Windows 或安全软件独立产生的日志和缓存。

## 哪些文件是程序

版本固定在 [Makefile](Makefile) 的 VERSION = 0.1，默认构建直接输出到 out/。

| 文件或目录 | 分类及用途 |
| --- | --- |
| out/VolumeEdit.exe | **最终程序**：总音量客户端，双击运行 |
| out/uninstall.exe | **最终程序**：图形卸载工具 |
| out/VolumeEditBroker.exe | **旧版兼容程序**：旧服务恢复与清理，正常用户不直接运行 |
| out/product.id | 必须与 EXE 一起保留的部署标记 |
| out/VolumeEdit-portable-v0.1-x64.zip | **最终发布包**，完整解压即可运行 |
| out/SHA256SUMS.txt、out/*.zip.sha256 | 文件及 ZIP 校验清单 |
| out/README.md、DESIGN.md、LICENSE | 产品文档及许可证 |
| out/BUILD_STATUS.md | 人工验证报告，不是程序 |
| out/reports/test-results.xml、test-results.log | 自动测试报告，不随 ZIP 发布 |
| out/reports/*-preview.png | 开发界面预览，不随 ZIP 发布 |
| out/reports/legacy-reset-* | 本机旧配置备份和迁移结果，不提交或发布 |
| build/ | 中间文件、测试及诊断工具，不是产品 |

Windows x64、MSVC C++20、静态运行库，无需 Visual Studio 或证书。源代码 Apache-2.0。GitHub Code → Download ZIP 是源码；普通用户使用编译后的 portable ZIP，或从 [Actions](https://github.com/KuGouOfficial/VolumeEdit/actions) 下载 VolumeEdit-portable-x64 产物。

技术依据：[总音量 dB 接口](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-setmastervolumelevel)、[设备范围及步进](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-getvolumerange)、[硬件及软件控制](https://learn.microsoft.com/en-us/windows/win32/coreaudio/endpointvolume-api)。硬件控制可作用于共享和独占输出；软件控制的独占输出可能绕过本接口，ASIO 行为依赖驱动。
