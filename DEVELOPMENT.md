# 开发说明

普通用户直接解压 ZIP 并双击 EXE，使用步骤见 README.md。以下命令仅供源代码构建者。

需要 Windows x64、Visual Studio C++ 桌面开发工具、Windows SDK、CMake 和 Ninja。无需 WDK、驱动构建工具或签名证书。静态链接 C++ 运行库。

## Makefile 入口

根目录 Makefile 兼容 Visual Studio 自带的 NMake 和 GNU Make。打开 Visual Studio 的“x64 Native Tools Command Prompt”或 Developer PowerShell，切换到仓库根目录运行：

```powershell
nmake /f Makefile
nmake /f Makefile test
nmake /f Makefile package
nmake /f Makefile rebuild
```

若使用 GNU Make，将 `nmake /f Makefile` 换为 `make`。无需另装 GNU Make，MSVC 自带的 NMake 即可构建。

| 目标 | 行为 |
| --- | --- |
| all / build（默认） | 编译，正式 EXE 直接输出到 out/；不执行测试，不生成新 ZIP |
| test | 编译并运行全部 CTest，更新 out/reports/ 中的自动报告 |
| package | 编译、测试通过后生成 out/ 中的 ZIP 及两类校验文件 |
| rebuild | 清理 CMake 编译产物后重新编译并测试；不删除用户 state 或未知文件 |

也可以在普通 PowerShell 中直接执行同一套脚本；脚本使用 vswhere 和 VsDevCmd 初始化 MSVC 工具链：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1
```

build.ps1 的 `-SkipTests` 对应 build 目标，`-Clean` 对应 rebuild。构建前请从托盘退出此输出目录中的客户端，避免运行中的 EXE 被占用。更新会保留 state；不要把未知产品目录作为 out。

## 版本号

产品版本唯一来源是根目录 Makefile 中的 `VERSION = 0.1`。升级时修改该行，再执行 package。支持 major.minor 或 major.minor.patch 数字格式，不通过命令行覆盖版本。

CMake 和 PowerShell 打包脚本直接读取同一行。当前对外显示 v0.1，Windows 文件及程序集版本为 0.1.0.0。显示文字、PE 版本资源、生成的 manifest 和 ZIP 名称均由此生成。修改 Makefile 会使现有 CMake/Ninja 工程重新配置。发布时同步更新 README.md、DESIGN.md 等文档中的版本说明；配置与恢复记录的 schema 版本独立维护。

## 输出目录

```text
out/
  VolumeEdit.exe                       最终客户端，双击运行
  uninstall.exe                        最终卸载工具，双击运行
  VolumeEditBroker.exe                  权限辅助，由客户端部署为服务
  product.id                           必须保留的部署标识
  README.md / DESIGN.md / LICENSE      产品文档和许可证
  BUILD_STATUS.md                      人工维护的验证记录
  SHA256SUMS.txt                       发布文件校验清单（package 生成）
  VolumeEdit-portable-v0.1-x64.zip      最终发布包（package 生成）
  VolumeEdit-portable-v0.1-x64.zip.sha256 ZIP 校验值（package 生成）
  reports/
    test-results.xml                   CTest JUnit 自动报告
    test-results.log                   CTest 运行日志
    client-preview.png                 客户端检查预览
    uninstall-preview.png              卸载界面检查预览
build/
  *_tests.exe                          开发测试程序，不是产品
  Testing/                             CTest 内部缓存与详细日志
  generated/                           生成头文件及 manifest
  CMakeFiles/ 等                        编译中间文件，不发布
```

三个 EXE 是产品程序，客户端和卸载程序供用户双击，辅助程序由客户端部署；运行时必须保留同目录 product.id。普通 build 不会刷新旧 ZIP、校验清单或测试报告，需要发布时执行 package，查看自动报告时核对时间。正常运行在 out/state 中保存设置与恢复记录；报告目录仅由开发检查生成。

ZIP 根目录包含三个 EXE、product.id、README.md、DESIGN.md、BUILD_STATUS.md、LICENSE 和 SHA256SUMS.txt，共 9 个文件。明确清单打包排除 state、reports、测试程序、调试符号和未知文件，不会将整个 out 目录递归打包或删除。卸载只清理产品文件与本部署状态，保留 ZIP、报告、源代码和用户自建内容。

## 检查与预览

```powershell
nmake /f Makefile test
.\out\VolumeEdit.exe --smoke
.\out\uninstall.exe --smoke
.\out\VolumeEdit.exe --snapshot
.\out\uninstall.exe --snapshot
```

GUI smoke/snapshot 不启动音量工作线程，不更改真实音量或开机启动项。PNG 只绘制程序自身窗口，保存在 out/reports。正常运行需要 product.id 标记，开发预览不需要标记。

self_delete.ps1 和 uninstall_cleanup.ps1 从 out 复制已构建的正式 EXE，在 build 下的随机专用目录进行检查。检查自身清理及用户文件保留，不改动真实音量或注册表；--uninstall-smoke 拒绝含 state 的目录。其他测试使用独立工作区文件与模拟数值。算法检查复用生产逻辑。CTest 日志保存在 out/reports，同时 build/Testing 保留内部诊断缓存。

公开仓库仅维护当前免驱动实现。历史驱动、安装器、服务、IPC 和 DSP 不参与当前构建或发布。

图标资源位于 assets/volumeedit.svg / .ico / .png。正常构建直接嵌入 ICO；如需重新生成，使用安装了 Pillow 的 Python 运行 assets/generate_icons.py。发布包无需外部图标文件。

## 持续集成

.github/workflows/build.yml 在 Windows runner 上调用同一 package.ps1。成功构建的 `VolumeEdit-portable-x64` 产物包含最终 ZIP 及 ZIP 校验值；`VolumeEdit-test-reports` 产物独立上传自动报告、日志和预览，测试失败时也尝试保留诊断文件。

工作流只申请 contents: read，不自动发布 Release。无需 WDK、Python 或音频硬件；只有主动重新生成图标时需要 Pillow。

## 权限辅助试验

当前运行中的 EXE 会阻止原路径覆盖。为保留旧版，本地试验可使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -OutputDirectory out/service-trial
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1 -OutputDirectory out/service-trial
```

默认 Makefile 入口仍直接输出到 out/。备用输出仅允许位于 out/ 内，测试报告跟随输出目录。

辅助服务代码是试验实现，默认不注册任何系统服务。用户通过 GUI“启用权限辅助”操作，UAC 后将固定三文件部署到 Program Files 下按 GUID 命名的受保护目录。helper 和 helper_pipe 自动检查无需管理员权限，不改音量、注册表或服务；真实注册和清理仍需实机验证。

仅用于诊断的 --setup-helper-only 入口会请求 UAC 并启用辅助，但不启动音量线程或创建托盘。--ownership-probe PID 只读比较普通查询与辅助查询，输出 reports/ownership-probe.txt；0=Unknown、1=Own、2=Other，assisted 表示辅助请求是否完成。不要将诊断入口作为普通用户使用流程。

服务是窄接口的 LocalSystem 权限代理。修改 IPC、受保护文件 ACL、所有权判断或卸载流程时必须运行权限门禁和真实管道回归检查，并单独验证 UAC、SCM 启动/移除与 Program Files 清理。原 releases/v0.1 分支及 v0.1 标签不因本地试验改写。
