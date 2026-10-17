# 上游基线与同步说明

## 1. 基线

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/Alexays/Waybar |
| 基线提交 | `4e207a0cb51a9eb89b4245d90245721f5e39bf06` |
| 提交日期 | 2026-10-11 |
| 上游版本 | `0.15.0`（`meson.build` 中 `project(version:)`） |
| 上游分支 | `master` |
| 许可证 | MIT（见仓库根 `LICENSE`） |

本仓库以该提交为基线做 **Windows 移植**。导入方式见 §3。

## 2. 为什么是"剔除式快照"而不是完整克隆

上游仓库根目录存在 **Windows 不允许的文件名**（含 `:`）：

```
.github/wiki/extras/Module:-Clock.md
.github/wiki/extras/Module:-Hyprland.md
...
```

已在 Windows 上实测：`git clone`、`git checkout`、`git archive` 全部因 `invalid path` 失败。因此本仓库只导入移植所需的子树，并记录被剔除的部分。

**保留**：`src/`、`include/`、`resources/`、`man/`、`test/`、`subprojects/`、`meson.build`、`meson_options.txt`、`LICENSE`、`.clang-format`、`.editorconfig`，以及上游 `README.md`（存档于 `docs/UPSTREAM-README.md`）。

**剔除**（不属于移植目标，或与 Windows 冲突）：

| 路径 | 原因 |
|---|---|
| `.github/` | 含非法文件名；CI 由本仓库自带 workflow 替代 |
| `nix/`、`flake.nix`、`flake.lock`、`default.nix`、`.envrc` | Nix 打包，与 Windows 无关 |
| `Dockerfiles/` | Linux 容器构建 |
| `protocol/` | Wayland 协议 XML 与生成代码；Windows 端口不编译 |
| `_codeql_detected_source_root` | 上游误提交的空标记文件 |
| `resources/custom_modules/__pycache__/` | Python 字节码缓存，非源码 |

## 3. 导入 / 同步流程

### 3.1 首次导入（已执行）

```bash
# 在 WSL（ext4 文件系统）中克隆，避免 Windows 非法文件名问题
git clone --depth 1 --branch master \
  https://v4.gh-proxy.org/https://github.com/Alexays/Waybar.git ~/waybar
cd ~/waybar && git log -1 --format='%H'

# 仅复制保留子树到 Windows 仓库
cp -a src include resources man test subprojects \
      meson.build meson_options.txt LICENSE README.md \
      .clang-format .editorconfig \
      /mnt/d/data/git/WaybarForWindows/
rm -rf /mnt/d/data/git/WaybarForWindows/resources/custom_modules/__pycache__
```

### 3.2 后续同步上游

`upstream` remote 指向镜像站（`github.com` 的 HTTPS 在本机不可达）：

```powershell
git remote add upstream https://v4.gh-proxy.org/https://github.com/Alexays/Waybar.git
git fetch upstream master --depth 1
git diff --stat HEAD upstream/master -- src include meson.build meson_options.txt
```

同步原则：

1. **优先把改动写成 `#ifdef _WIN32` 分支**，让上游文件的"非 Windows 路径"保持原样，降低合并冲突。
2. Windows 专有实现放在 `src/win32/`、`include/win32/`、`src/modules/win32/`，不修改上游目录结构。
3. 合并后必须重跑 `scripts\build.ps1` 与 `scripts\test.ps1`。
4. 基线变更需同时更新本文件与 `docs/MODULE-MATRIX.md`。

## 4. 构建方式差异

上游使用 meson（≥ 0.59），本仓库沿用 meson，仅新增 `is_windows` 分支：裁剪 wayland/layer-shell/dbus/udev 等依赖，追加 Win32 源文件与系统库。详见 `docs/BUILD.md`。
