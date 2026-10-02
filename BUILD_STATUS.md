# 构建与验证记录

2026-10-02，VolumeEdit v0.1 已切换到 Windows 总音量 dB 模式。Windows x64、MSVC 19.51、SDK 10.0.26100、C++20、静态 CRT，未签名。

## 自动检查

无编译警告，CTest **11/11 通过**：config、routing、backup、endpoint、portable、helper、ui_smoke、uninstall_ui_smoke、helper_pipe、self_delete、uninstall_cleanup。

配置检查包括 schema v4 和旧会话偏移迁移为总音量 0 dB。backup 覆盖跨部署衰减链合并和冲突拒绝；endpoint 覆盖设备上下限、dB 偏移、中断匹配、外部更改、严格备份及原子存储。清理包含新 endpoint-volumes.txt 及临时文件，保留自建文件。helper 检查只为旧版兼容保留。

自动检查不改真实音量、Run 或服务。最新报告为 out/reports/test-results.xml、test-results.log，核对时间判断对应构建。

## 本机实测

- C-Media 默认输出：−45～0 dB、1 dB 步进，支持硬件音量。
- 普通权限写入总音量偏移、音量复位、退出恢复均成功。
- 工作线程停止/再启动成功，Windows 外部修改优先，退出未覆盖外部值。
- 检查期间临时静音，结束恢复开始时总音量和静音；日志 out/reports/endpoint-live-check.txt。
- 迁移前备份旧配置，按可确认衰减链合并两个部署。Chrome、英雄联盟游戏、Riot 的当前会话值核对为 1.0、1.0、0.0860994。PowerShell 通过隐藏零 PCM 流重建会话后，旧任务已处理。
- 最后的 WSL 会话通过静音 PulseAudio 流重新建立后处理，当前旧恢复记录已清空，两个部署的 volumes.txt 均不存在。恢复仅覆盖匹配本工具写入的值，外部修改保留。
- 旧试验权限服务已通过 UAC 清理；SCM 不存在、服务进程退出、Program Files 专用目录与 helper.txt 标记均不存在。

为避免旧会话恢复突然变响，迁移先将设备总音量降到 −45 dB，保持原静音标志。后续用户手动音量调整保留。原配置备份和迁移结果在 out/reports/legacy-reset-20261002-*，不提交、不发布。

## 产物分类

| 类别 | 位置 |
| --- | --- |
| 最终程序 | out/VolumeEdit.exe、out/uninstall.exe |
| 旧版兼容程序 | out/VolumeEditBroker.exe |
| 最终 ZIP 及校验 | out/VolumeEdit-portable-v0.1-x64.zip、同名 .sha256、SHA256SUMS.txt |
| 自动报告及预览 | out/reports/ |
| 编译、测试和实机诊断工具 | build/，不发布 |
| 人工验收记录 | 本文，不是自动报告 |

正常构建保留 state、ZIP、历史目录和用户文件。发布 ZIP 仅 9 个产品文件。产品版本由 Makefile 固定为 v0.1，不移动历史发布 tag。

## 待验收

不同 Windows/驱动、热插拔和蓝牙重连、系统重启加载、独占/ASIO 行为、不可访问设备恢复、设备离线时的完整卸载仍待检查。设备最低 −45 dB 不能由本接口突破，读写与外部更改仍有短暂竞态；不承诺采样级额外增益。
