# 开发说明

普通用户直接解压 ZIP 并双击 EXE，使用步骤见 README.md。以下命令仅供源代码构建者。

需要 Windows x64、Visual Studio C++ 桌面开发工具、Windows SDK、CMake 和 Ninja。无需 WDK、驱动构建工具或签名证书。静态链接 C++ 运行库。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1
```

build.ps1 使用 vswhere 和 VsDevCmd 初始化 MSVC，配置 CMake release preset，构建 VolumeEdit.exe、uninstall.exe 和测试，再运行 CTest。-SkipTests 跳过测试，-Clean 重新构建。

package.ps1 只复制明确的发布文件，不包含旧驱动、安装器、状态、调试符号或开发测试。输出 out/VolumeEdit-portable-v0.1 和 out/VolumeEdit-portable-v0.1-x64.zip。调试产物留在 build。

```powershell
ctest --test-dir build --output-on-failure
.\build\VolumeEdit.exe --smoke
.\build\uninstall.exe --smoke
.\build\VolumeEdit.exe --snapshot
.\build\uninstall.exe --snapshot
```

GUI smoke/snapshot 不启动音量工作线程，不更改真实音量或开机启动项。snapshot 只绘制程序自身窗口，PNG 保存在 build。程序正常运行需要 ZIP 中的 product.id 标记，开发预览不需要标记。

self_delete.ps1 在 build 下创建随机、专用、带产品标记的卸载 EXE 副本，验证进程退出后 EXE、标记和空目录全部消失。无音量或注册表修改。其他测试使用独立工作区文件与模拟数值；会话策略测试复用生产逻辑。

公开仓库仅维护当前免驱动实现。原驱动、安装器、服务、IPC 与 DSP 的历史实现不参与当前构建或发布。

卸载集成检查使用 build/uninstall-test-随机GUID 目录，运行保留给开发检查的 --uninstall-smoke 入口；该入口拒绝含 state 的目录。验证正常卸载流程及最终自身清理，同时确认自建文件保留，不修改真实音量和注册表。



图标资源位于 assets/volumeedit.svg / .ico / .png。正常构建直接嵌入 ICO；如需重新生成，使用安装了 Pillow 的 Python 运行 assets/generate_icons.py。客户端与卸载程序复用该资源，发布包无需外部图标文件。

## 持续集成

.github/workflows/build.yml 在 Windows runner 上调用相同的 package.ps1，完成构建、CTest 和 ZIP 打包。工作流只申请 contents: read，结果作为构建产物上传，不自动发布 Release。无需 WDK、Python 或音频硬件；图标生成器仅在主动重新制作图标时需要 Pillow。

## 版本号

CMakeLists.txt 中的工程版本为 0.1.0，对外显示 v0.1。客户端、卸载界面、Windows 文件版本资源和默认 ZIP 名均由工程版本生成；Windows 文件/程序集版本为 0.1.0.0。设置与恢复记录的 schema 版本独立于产品版本，不因此次重命名重置用户配置。
