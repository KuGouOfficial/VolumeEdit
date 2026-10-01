# VolumeEdit v0.1

<p><img src="assets/volumeedit.png" alt="VolumeEdit 图标" width="96"></p>

[Apache-2.0 许可证](LICENSE) · [设计文档](DESIGN.md) · [开发构建](DEVELOPMENT.md) · [问题反馈](https://github.com/KuGouOfficial/VolumeEdit/issues)

Windows 耳机音量微调工具，使用 MSVC C++ 编写。免驱动、免安装向导，解压后双击运行。当前版本调节应用会话音量：**−40～+40 dB**，步进 0.1 dB。程序采用深蓝底、青色耳机和橙色调节钮的专属图标，文件、窗口和托盘统一显示。

## 使用

1. 下载并解压 VolumeEdit-portable-v0.1-x64.zip 到自己可写的独立目录，例如文档文件夹下的 VolumeEdit。请完整解压，不要在压缩包内运行。
2. 双击 VolumeEdit.exe。首次运行固定为 **0.0 dB**，不会降低已有音量；0 dB 有蓝色基准文字及滑块中央标记。
3. 输出设备默认使用 Windows 当前默认输出，切换 Windows 默认设备后自动跟随。无需修改 Windows 声音输出，也可以手动指定一个设备。
4. 拖动滑块或输入 −40～40 内的数值后按回车，调节当前用户在该输出设备上的应用会话音量。负值降低音量，正值相对原会话音量提高，0 dB 位于滑块中央；正值达到会话 100% 上限后不再提高，状态区会显示达到上限的会话数。耳机太响时可先尝试 −10 dB，再逐步调整。**音量复位**返回 0 dB 并尝试恢复原会话音量。
5. 勾选唯一的 **开机启动** 开关，下次当前用户登录 Windows 后自动运行到托盘。不需要管理员权限。关闭窗口继续在托盘运行；右键托盘选择“退出并恢复音量”才会停止处理。

修改后的调节值和设备选择保存在本目录的 state 中，下次运行会加载。0 dB 是首次使用和复位的基准。请保留整个目录；移动目录后重新切换一次开机启动，以更新启动路径。

旧版保存的调节值若低于 −40 dB，新版会按 −40 dB 加载，输出设备设置保留。其他合法配置继续加载。更新前先从托盘退出旧版，再用新版 ZIP 覆盖原目录的产品文件，并保留 state；不要同时运行两份复制的 state。

## 卸载

双击同目录的 **uninstall.exe**，点击“卸载”。它会关闭本目录的客户端、恢复本工具记录的会话原音量、删除属于此部署的当前用户启动项，以及产品文件和 state。最后自动关闭卸载窗口并显示清理结果，不需要输入命令。

如果原设备断开、相关应用已经关闭、恢复记录损坏或文件被占用，卸载会提示未完成。恢复音量失败时不会删除恢复记录和产品文件：请重新连接原设备、打开相关应用后重试。不要先手动删除 state，否则无法按记录恢复原音量。

最后的自身文件清理由 Windows 内置 PowerShell 在隐藏进程中完成；它等待卸载程序退出，只删除明确指定的文件，不生成临时脚本、服务或计划任务。若系统禁止 PowerShell 运行，最后清理会失败；保留目录并重试。产品文件删除后仍存在的自建文件会保留。若最后清理只剩 product.id，请重新解压卸载程序到原目录后重试。

本版本不创建驱动、虚拟音频设备、Windows 服务、计划任务、开始菜单项或“已安装的应用”记录，不写 HKLM。唯一持久化注册表项是勾选开机启动时写入的当前用户 Run 值。卸载只清理它自己的资源。

## 能做什么

本版本使用 Windows 应用会话音量接口，按原会话音量乘以 10^(dB/20) 调节，并将结果限制在 0～100%。例如 −20 dB 将会话的标量音量乘以 0.1。不会改变 Windows 主音量，也不会新增静音开关或 dB 预设。

这不是最终混音后增益。正 dB 只能提高原本未达上限的会话音量，例如原会话标量 0.1 在 +20 dB 时达到 1.0；原会话已为 1.0 时，+40 dB 也不会进一步放大音频。会话标量上限来自 [Windows 会话音量接口](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-isimpleaudiovolume-setmastervolume)。ASIO、独占输出、受保护或无访问权限的会话可能不受影响；指定到其他设备的应用不会由当前选中设备的控制覆盖。检测新会话采用约 50 ms 轮询，新应用最初的声音可能在衰减生效前播放，因此不能保证每个音频采样或开机早期声音都被降低。

应用会话音量可能由 Windows 记忆。程序每次修改之前先保存原音量，正常退出、复位和卸载时恢复；异常退出后可重新运行恢复。若你或应用后来改变会话音量，程序将新值作为基准，退出时保留这次更改。多实例恢复有歧义时会保留记录，避免猜测覆盖。

## 环境与源代码

发布包是 Windows x64 原生 GUI 程序，静态链接 C++ 运行库。无需安装 Visual Studio、WDK 或证书。已在 Windows 环境完成构建及 8 项自动检查；Windows 10/11、设备热插拔和真实耳机听感仍需实际使用验收。

源代码沿用仓库的 [Apache-2.0 许可证](LICENSE)。设计见 [DESIGN.md](DESIGN.md)，构建方法见 [DEVELOPMENT.md](DEVELOPMENT.md)，验证记录见 [BUILD_STATUS.md](BUILD_STATUS.md)。公开仓库只包含当前免驱动实现，不提交运行配置、构建缓存或历史驱动产物。

Windows build 工作流会编译、运行检查并上传 ZIP 构建产物，可在 [Actions](https://github.com/KuGouOfficial/VolumeEdit/actions) 中下载。GitHub 的 Code → Download ZIP 下载的是源码，需要开发工具构建；普通用户请使用编译后的 portable ZIP。

## 构建结果中哪些可以运行

版本固定在根目录 [Makefile](Makefile) 的 `VERSION = 0.1`。源码构建后，正式程序直接生成到 `out/`，无需再到版本子目录查找。以下命令仅供开发者；普通用户仍然解压 ZIP、双击 EXE。

| 文件或目录 | 用途 | 普通用户是否需要 |
| --- | --- | --- |
| `out/VolumeEdit.exe` | **最终程序**：音量客户端和托盘 | 需要，双击运行 |
| `out/uninstall.exe` | **最终程序**：图形卸载工具 | 需要，卸载时双击 |
| `out/product.id` | 部署标识，必须与两个 EXE 放在一起 | 需要保留 |
| `out/VolumeEdit-portable-v0.1-x64.zip` | **最终发布包**，执行打包后生成；完整解压即可使用 | 推荐下载这一项 |
| `out/SHA256SUMS.txt` | ZIP 内各产品文件的 SHA-256 校验清单 | 可用于校验 |
| `out/VolumeEdit-portable-v0.1-x64.zip.sha256` | ZIP 文件本身的 SHA-256 校验值 | 可用于校验 |
| `out/README.md`、`out/DESIGN.md`、`out/LICENSE` | 使用说明、设计文档和许可证 | 文档，不是程序 |
| `out/BUILD_STATUS.md` | 随包提供的人工维护验证记录及待验收项 | 报告，不是程序 |
| `out/reports/test-results.xml`、`test-results.log` | 最近一次自动测试的 JUnit 报告和日志 | 开发报告，不随发布 ZIP 提供 |
| `out/reports/client-preview.png`、`uninstall-preview.png` | GUI 检查生成的界面预览 | 开发报告，不随发布 ZIP 提供 |
| `build/*_tests.exe`、`build/Testing/`、其他 `build/` 内容 | 测试程序、CTest 缓存、编译中间文件 | 不需要，不随发布 ZIP 提供 |

`build` 目标只编译；`test` 目标编译并生成报告；`package` 目标编译、测试通过后生成 ZIP 和校验值。`build` 不更新旧测试报告，查看报告时应核对时间。详细命令见 [DEVELOPMENT.md](DEVELOPMENT.md)。

GitHub Actions 的 `VolumeEdit-portable-x64` 产物包含最终 ZIP 及其校验值；`VolumeEdit-test-reports` 产物是开发测试报告。源码目录中的卸载工具仅清理本部署产品和运行配置，保留构建报告、ZIP、源代码及自建文件。
