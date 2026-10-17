# 安装指南（setup.exe）

仓库提供原生安装程序 `setup.exe`，用于**一键安装**并完成 Windows 桌面集成：

| 项目 | 位置 / 行为 |
|---|---|
| 安装目录 | 默认 `%LOCALAPPDATA%\Programs\Waybar`（无需管理员权限） |
| 程序文件 | `bin\waybar.exe` + 全部第三方 DLL + `share\`/`lib\`/`etc\`（GTK 运行期资源） |
| 配置 | `config\config.jsonc`、`config\style.css`（可被 `%USERPROFILE%\.config\waybar\` 覆盖） |
| 开始菜单 | `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Waybar\`：`Waybar.lnk`、`Uninstall Waybar.lnk` |
| 卸载注册表 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Waybar`（设置 → 应用 中可见） |
| 开机启动 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Waybar` |
| App Paths | `HKCU\Software\Microsoft\Windows\CurrentVersion\App Paths\waybar.exe`（可在 Win+R 输入 `waybar`） |

## 1. 图形化安装（一键）

1. 解压 `waybar-<version>-setup-windows-x64.zip`；
2. 双击 `setup.exe`，确认安装位置后点击「是」；
3. 安装完成后从开始菜单启动 **Waybar**（默认已加入开机启动）。

## 2. 静默安装 / 自定义

```powershell
setup.exe /S                              # 静默安装（默认目录 + 开机启动）
setup.exe /S /no-autostart                # 静默安装，不设开机启动
setup.exe /S /dir="D:\Apps\Waybar"        # 指定安装目录
setup.exe --uninstall /S                  # 静默卸载
```

安装程序会先结束正在运行的 `waybar.exe` 实例，再覆盖复制文件，因此可重复执行用于升级。

## 3. 开机启动

默认写入 `HKCU\...\CurrentVersion\Run\Waybar`，命令形如：

```
"C:\Users\<user>\AppData\Local\Programs\Waybar\bin\waybar.exe" \
  --config "...\config\config.jsonc" --style "...\config\style.css"
```

- 关闭/开启：任务管理器 → 启动应用 → **Waybar**；或删除/重建该注册表值；
- 重新安装时加 `/no-autostart` 可去掉启动项；
- 卸载会自动删除该启动项。

> 路径使用 UTF-16 命令行解析（`CommandLineToArgvW`），因此中文用户名目录可用。

## 4. 卸载

任选其一：

- 设置 → 应用 → **Waybar for Windows** → 卸载；
- 开始菜单 → Waybar → **Uninstall Waybar**；
- 安装目录下 `uninstall.exe --uninstall`（`/S` 静默）。

卸载会删除开始菜单快捷方式、注册表项（卸载信息/启动项/App Paths）与整个安装目录；
程序目录由安装器在进程退出后自行删除（`rmdir /s /q`）。

## 5. 构建安装包（开发者）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\make-installer.ps1
# -SkipPayload：复用已有 dist\waybar-<ver>-windows-x64
```

产物：

- `dist\waybar-<version>-setup\`（`setup.exe` + `payload\` + `INSTALL.txt`）
- `dist\waybar-<version>-setup-windows-x64.zip`

`setup.exe` 由 `installer/setup.cpp` 编译（MSYS2 g++，静态链接，无外部依赖）。
CI（`.github/workflows/build-windows.yml`）会同时上传便携包与安装包 artifact。
## 6. 附带主题

安装包内含 `themes\` 目录（也可在安装目录的 `themes\` 里找到）：

| 主题 | 说明 |
|---|---|
| `zek-niri-windows` | ZEK-niri 主题的 Windows 适配版（含配色切换脚本，见 `docs/CONFIG-COMPAT.md`） |
| `all-modules` | 启用全部已实现模块的示例配置 |

应用方式：把主题目录内容复制到 `%LOCALAPPDATA%\Programs\Waybar\config\`，然后重启 Waybar
（开始菜单 → Waybar）。原默认配置可在安装后备份为 `config.default\` 便于还原。

```powershell
$dst = "$env:LOCALAPPDATA\Programs\Waybar\config"
Copy-Item "$env:LOCALAPPDATA\Programs\Waybar\themes\zek-niri-windows\*" $dst -Recurse -Force
& "$env:LOCALAPPDATA\Programs\Waybar\bin\waybar.exe" --reload
```

字体：该主题使用 Nerd Font 字形，推荐安装 **FiraCode Nerd Font**（`style.css` 已首选它）。