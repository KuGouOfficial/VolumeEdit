# 开发说明

普通用户直接解压 ZIP、双击 EXE，见 README.md。以下命令仅供开发者。

Windows x64、Visual Studio C++ 桌面工具、Windows SDK、CMake、Ninja，静态 MSVC CRT，无需 WDK、驱动或证书。

## Makefile

在 x64 Developer PowerShell/Native Tools Command Prompt 的仓库根目录运行：

```powershell
nmake /f Makefile
nmake /f Makefile test
nmake /f Makefile package
nmake /f Makefile rebuild
```

GNU Make 可将 nmake /f Makefile 换为 make。

| 目标 | 行为 |
| --- | --- |
| all / build | 编译正式程序到 out，不运行测试、不更新 ZIP |
| test | 编译并执行 11 项 CTest，报告到 out/reports |
| package | 编译、测试、严格清单打包 ZIP 和 SHA-256 |
| rebuild | 清理编译产物再编译测试；不删除 state 或用户文件 |

普通 PowerShell 也可执行 tools/build.ps1 或 tools/package.ps1，脚本用 vswhere / VsDevCmd 初始化工具链。SkipTests 对应 build，Clean 对应 rebuild。程序运行时请先从托盘退出，避免覆盖锁定的 EXE。备用 OutputDirectory 只允许 out 内目录。

## 版本和产物

Makefile 的 VERSION = 0.1 是唯一版本来源；CMake 和 PowerShell 直接读取，显示 v0.1，PE/manifest 0.1.0.0。配置 schema v4 独立于产品版本。

out/VolumeEdit.exe、uninstall.exe 是最终用户程序；VolumeEditBroker.exe 只保留旧版本权限恢复及服务清理兼容，正常总音量控制不部署服务。product.id 必须保留。ZIP 根目录是三个 EXE、product.id、README、DESIGN、BUILD_STATUS、LICENSE、SHA256SUMS，共 9 个文件。

out/reports 下 JUnit、日志、PNG 是开发报告；迁移备份 legacy-reset-* 也是私有开发数据，不发布。build 下测试 EXE、对象、库、CTest 缓存及生成资源均非最终程序。state 是用户配置和恢复依据，不加入 ZIP 或 Git。卸载保留 ZIP、报告和未知文件。

## 验证与预览

```powershell
.\out\VolumeEdit.exe --smoke
.\out\uninstall.exe --smoke
.\out\VolumeEdit.exe --snapshot
.\out\uninstall.exe --snapshot
```

GUI smoke/snapshot 不启动音量线程，不改真实音量或启动项。PNG 只绘制自身窗口，保存到 out/reports。CTest 的数学、配置、迁移、权限、管道和清理检查不改真实音量或注册表；清理使用 build 下的带专用标记随机目录。endpoint 测试覆盖生产目标函数和严格备份。

真实总音量检查 tests/endpoint_live.cpp 是独立的 EXCLUDE_FROM_ALL 目标，不属于 CTest/CI。必须明确选择 --confirm-real-audio，并提供 build 下名称以 endpoint-live- 开头且不存在的临时目录。检查会临时静音、设置真实默认设备、运行 AudioEngine，验证写入/复位/退出/重启/外部更改优先，退出恢复原音量与静音。不要在不适合暂时改变系统声音的情况下运行。

```powershell
cmake --build build --target endpoint_live_check
.\build\endpoint_live_check.exe --confirm-real-audio "$PWD\build\endpoint-live-manual"
```

正常控制使用 IAudioEndpointVolume。旧会话恢复只存在 legacy_recovery.cpp，待恢复数量非错误弹窗。跨目录迁移必须保留原记录备份，严格验证衰减链，不能直接重置所有应用音量。

旧 helper/helper_pipe 回归为兼容保留。--remove-helper-only 用于同目录旧部署的 UAC 清理，不启动音量线程。--ownership-probe PID 为只读旧权限诊断；新总音量功能不调用这条路径。不再提供 --setup-helper-only 的注册入口；普通用户通过 GUI 清理旧辅助。

图标在 assets/volumeedit.svg/.ico/.png，直接嵌入 ICO；仅重新生成时需要 Python/Pillow。GitHub Actions 调用同一 package.ps1，分开上传 VolumeEdit-portable-x64 和 VolumeEdit-test-reports，不自动发布 Release。历史 releases/v0.1 与 v0.1 tag 保持原提交。