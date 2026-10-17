// Waybar for Windows 安装程序（setup.exe）
//
// 默认安装到 %LOCALAPPDATA%\Programs\Waybar（无需管理员权限），并：
//   1) 复制 payload\ 下的程序与运行库（bin/share/lib/config）
//   2) 写入卸载注册表项（设置 → 应用 中可见）
//   3) 创建开始菜单快捷方式（Waybar / 卸载 Waybar）
//   4) 写入开机启动项 HKCU\...\CurrentVersion\Run\Waybar
//   5) 注册 App Paths，使 `waybar` 可在运行框/命令行直接调用
//
// 用法：
//   setup.exe                    交互式安装（弹窗确认）
//   setup.exe /S                 静默安装
//   setup.exe /S /no-autostart   静默安装且不设开机启动
//   setup.exe /dir="D:\Apps\Waybar"  指定安装目录
//   setup.exe --uninstall [/S]   卸载（也可用设置→应用里“卸载”）

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace {

constexpr const wchar_t* kAppName = L"Waybar for Windows";
constexpr const wchar_t* kPublisher = L"WaybarForWindows";
constexpr const wchar_t* kVersion = L"0.15.0";
constexpr const wchar_t* kUninstallKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Waybar";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kAppPathsKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\waybar.exe";

std::wstring g_exe_dir;

std::wstring exe_dir() {
  static std::wstring cached;
  if (cached.empty()) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
      const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
      if (len == 0) break;
      if (len < buffer.size()) {
        buffer.resize(len);
        break;
      }
      buffer.resize(buffer.size() * 2);
    }
    cached = buffer.substr(0, buffer.find_last_of(L'\\'));
  }
  return cached;
}

std::wstring join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\') return a + b;
  return a + L"\\" + b;
}

bool file_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring env(const wchar_t* name) {
  DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) return {};
  std::wstring value(size, L'\0');
  const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
  if (written == 0) return {};
  value.resize(written);
  return value;
}

std::wstring payload_dir() {
  const std::wstring candidate = join(exe_dir(), L"payload");
  if (file_exists(join(candidate, L"bin\\waybar.exe"))) return candidate;
  // 允许 setup.exe 直接放在 dist 目录中（包含 bin/waybar.exe）
  if (file_exists(join(exe_dir(), L"bin\\waybar.exe"))) return exe_dir();
  return candidate;
}

std::wstring default_install_dir() {
  const std::wstring local = env(L"LOCALAPPDATA");
  return join(local.empty() ? env(L"USERPROFILE") : local, L"Programs\\Waybar");
}

bool ensure_dir(const std::wstring& path) {
  if (path.empty()) return false;
  if (file_exists(path)) return true;
  const std::wstring parent = path.substr(0, path.find_last_of(L'\\'));
  if (!parent.empty() && parent != path) {
    ensure_dir(parent);
  }
  return CreateDirectoryW(path.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool copy_file_checked(const std::wstring& src, const std::wstring& dst) {
  if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
    std::wprintf(L"  复制失败: %s (%lu)\n", src.c_str(), GetLastError());
    return false;
  }
  return true;
}

bool copy_tree(const std::wstring& src, const std::wstring& dst, unsigned long long& bytes) {
  if (!ensure_dir(dst)) {
    std::wprintf(L"  创建目录失败: %s\n", dst.c_str());
    return false;
  }

  WIN32_FIND_DATAW data{};
  HANDLE handle = FindFirstFileW(join(src, L"*").c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) {
    std::wprintf(L"  读取目录失败: %s\n", src.c_str());
    return false;
  }

  bool ok = true;
  do {
    const std::wstring name = data.cFileName;
    if (name == L"." || name == L"..") continue;

    const std::wstring from = join(src, name);
    const std::wstring to = join(dst, name);

    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      ok = copy_tree(from, to, bytes) && ok;
    } else {
      if (!copy_file_checked(from, to)) {
        ok = false;
      } else {
        bytes += (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
      }
    }
  } while (FindNextFileW(handle, &data));
  FindClose(handle);
  return ok;
}

bool remove_tree(const std::wstring& path) {
  if (!file_exists(path)) return true;

  WIN32_FIND_DATAW data{};
  HANDLE handle = FindFirstFileW(join(path, L"*").c_str(), &data);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = data.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring child = join(path, name);
      if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        remove_tree(child);
      } else {
        SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(child.c_str());
      }
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
  }
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
  return RemoveDirectoryW(path.c_str()) != 0;
}

void kill_running_instances() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;

  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"waybar.exe") == 0) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (process != nullptr) {
          TerminateProcess(process, 0);
          WaitForSingleObject(process, 2000);
          CloseHandle(process);
          found = true;
        }
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);

  if (found) {
    std::wprintf(L"  已停止正在运行的 waybar 实例\n");
    Sleep(300);
  }
}

bool set_reg_string(HKEY root, const std::wstring& subkey, const wchar_t* name,
                    const std::wstring& value) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return false;
  }
  const LONG result =
      RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                     static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

bool set_reg_dword(HKEY root, const std::wstring& subkey, const wchar_t* name, DWORD value) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS) {
    return false;
  }
  const LONG result = RegSetValueExW(key, name, 0, REG_DWORD,
                                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

void delete_reg_tree(HKEY root, const std::wstring& subkey) {
  RegDeleteTreeW(root, subkey.c_str());
}

void delete_reg_value(HKEY root, const std::wstring& subkey, const wchar_t* name) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
    RegDeleteValueW(key, name);
    RegCloseKey(key);
  }
}

bool create_shortcut(const std::wstring& lnk_path, const std::wstring& target,
                     const std::wstring& arguments, const std::wstring& workdir,
                     const std::wstring& description, const std::wstring& icon) {
  IShellLinkW* shell_link = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                              reinterpret_cast<void**>(&shell_link)))) {
    return false;
  }

  shell_link->SetPath(target.c_str());
  shell_link->SetArguments(arguments.c_str());
  shell_link->SetWorkingDirectory(workdir.c_str());
  shell_link->SetDescription(description.c_str());
  shell_link->SetIconLocation(icon.c_str(), 0);

  IPersistFile* persist = nullptr;
  bool ok = false;
  if (SUCCEEDED(shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist)))) {
    ok = SUCCEEDED(persist->Save(lnk_path.c_str(), TRUE));
    persist->Release();
  }
  shell_link->Release();
  return ok;
}

std::wstring start_menu_dir() {
  const std::wstring appdata = env(L"APPDATA");
  return join(join(appdata, L"Microsoft\\Windows\\Start Menu\\Programs"), L"Waybar");
}

std::wstring desktop_dir() {
  PWSTR path = nullptr;
  std::wstring result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
    result = path;
    CoTaskMemFree(path);
  } else {
    result = join(env(L"USERPROFILE"), L"Desktop");
  }
  return result;
}

std::wstring quote(const std::wstring& value) { return L"\"" + value + L"\""; }

std::wstring autostart_command(const std::wstring& install_dir) {
  return quote(join(install_dir, L"bin\\waybar.exe")) + L" --config " +
         quote(join(install_dir, L"config\\config.jsonc")) + L" --style " +
         quote(join(install_dir, L"config\\style.css"));
}

int show_message(const std::wstring& text, UINT flags) {
  return MessageBoxW(nullptr, text.c_str(), kAppName, flags);
}

bool is_silent(const std::vector<std::wstring>& args) {
  for (const auto& arg : args) {
    if (_wcsicmp(arg.c_str(), L"/S") == 0 || _wcsicmp(arg.c_str(), L"--silent") == 0) return true;
  }
  return false;
}

bool has_flag(const std::vector<std::wstring>& args, const wchar_t* flag) {
  for (const auto& arg : args) {
    if (_wcsicmp(arg.c_str(), flag) == 0) return true;
  }
  return false;
}

void remove_start_menu_shortcuts() {
  const std::wstring dir = start_menu_dir();
  DeleteFileW(join(dir, L"Waybar.lnk").c_str());
  DeleteFileW(join(dir, L"Uninstall Waybar.lnk").c_str());
  RemoveDirectoryW(dir.c_str());
}

bool install(const std::vector<std::wstring>& args) {
  const bool silent = is_silent(args);

  std::wstring install_dir = default_install_dir();
  for (const auto& arg : args) {
    if (arg.rfind(L"/dir=", 0) == 0) {
      std::wstring value = arg.substr(5);
      if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
      }
      if (!value.empty()) install_dir = value;
    }
  }

  const std::wstring payload = payload_dir();
  if (!file_exists(join(payload, L"bin\\waybar.exe"))) {
    show_message(L"找不到安装文件（payload\\bin\\waybar.exe）。\n\n请将 setup.exe 与 payload 目录放在一起后重试。",
                 MB_ICONERROR | MB_OK);
    return false;
  }

  const bool autostart =
      !has_flag(args, L"/no-autostart") && !has_flag(args, L"/NoAutostart");

  if (!silent) {
    const std::wstring message =
        std::wstring(L"即将安装 ") + kAppName + L" 到：\n\n" + install_dir +
        L"\n\n将创建开始菜单快捷方式" +
        (autostart ? L"和开机启动项" : L"（不设置开机启动）") +
        L"，并写入卸载注册表项。\n\n继续安装？";
    if (show_message(message, MB_ICONQUESTION | MB_YESNO) != IDYES) {
      return false;
    }
  }

  std::wprintf(L"==> 安装 %s 到 %s\n", kAppName, install_dir.c_str());

  kill_running_instances();

  if (!ensure_dir(install_dir)) {
    show_message(L"无法创建安装目录：\n" + install_dir + L"\n\n请检查权限或改用其它目录（/dir=...）。",
                 MB_ICONERROR | MB_OK);
    return false;
  }

  std::wprintf(L"==> 复制文件\n");
  unsigned long long bytes = 0;
  if (!copy_tree(payload, install_dir, bytes)) {
    show_message(L"文件复制失败，安装未完成。", MB_ICONERROR | MB_OK);
    return false;
  }

  const std::wstring waybar_exe = join(install_dir, L"bin\\waybar.exe");
  const std::wstring uninstall_exe = join(install_dir, L"uninstall.exe");
  CopyFileW(join(exe_dir(), L"setup.exe").c_str(), uninstall_exe.c_str(), FALSE);

  std::wprintf(L"==> 注册表与开始菜单\n");

  // 卸载信息（设置 → 应用）
  const std::wstring uninstall_key = kUninstallKey;
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, nullptr, std::wstring(kAppName));
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"DisplayName", kAppName);
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"DisplayVersion", kVersion);
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"Publisher", kPublisher);
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"InstallLocation", install_dir);
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"DisplayIcon", waybar_exe);
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"UninstallString",
                 quote(uninstall_exe) + L" --uninstall");
  set_reg_string(HKEY_CURRENT_USER, uninstall_key, L"QuietUninstallString",
                 quote(uninstall_exe) + L" --uninstall /S");
  set_reg_dword(HKEY_CURRENT_USER, uninstall_key, L"NoModify", 1);
  set_reg_dword(HKEY_CURRENT_USER, uninstall_key, L"NoRepair", 1);
  set_reg_dword(HKEY_CURRENT_USER, uninstall_key, L"EstimatedSize",
                static_cast<DWORD>((bytes + 1023) / 1024));

  // App Paths：允许在 Win+R / cmd 中直接输入 waybar
  set_reg_string(HKEY_CURRENT_USER, kAppPathsKey, nullptr, waybar_exe);
  set_reg_string(HKEY_CURRENT_USER, kAppPathsKey, L"Path", join(install_dir, L"bin"));

  // 开机启动
  if (autostart) {
    set_reg_string(HKEY_CURRENT_USER, kRunKey, L"Waybar", autostart_command(install_dir));
    std::wprintf(L"  已添加开机启动项（HKCU Run\\Waybar）\n");
  } else {
    delete_reg_value(HKEY_CURRENT_USER, kRunKey, L"Waybar");
    std::wprintf(L"  跳过开机启动项\n");
  }

  // 开始菜单
  const std::wstring menu = start_menu_dir();
  if (ensure_dir(menu)) {
    create_shortcut(join(menu, L"Waybar.lnk"), waybar_exe,
                    L"--config " + quote(join(install_dir, L"config\\config.jsonc")) +
                        L" --style " + quote(join(install_dir, L"config\\style.css")),
                    join(install_dir, L"bin"), L"启动 Waybar 状态栏", waybar_exe);
    create_shortcut(join(menu, L"Uninstall Waybar.lnk"), uninstall_exe, L"--uninstall",
                    install_dir, L"卸载 Waybar for Windows", uninstall_exe);
    std::wprintf(L"  已创建开始菜单快捷方式: %s\n", menu.c_str());
  } else {
    std::wprintf(L"  警告: 无法创建开始菜单目录\n");
  }

  // 释放旧实例占用（若安装目录在 PATH 中，使新版立即生效）
  if (!silent) {
    show_message(std::wstring(kAppName) + L" 安装完成。\n\n安装位置：" + install_dir +
                     L"\n开始菜单：Waybar" +
                     (autostart ? L"\n开机启动：已启用（可在任务管理器→启动 中关闭）" : L""),
                 MB_ICONINFORMATION | MB_OK);
  }
  std::wprintf(L"==> 安装完成\n");
  return true;
}

void schedule_self_delete(const std::wstring& dir) {
  // 由 cmd 在进程退出后删除整个安装目录（包含 uninstall.exe 自身）
  const std::wstring command =
      L"/c ping -n 2 127.0.0.1 >nul & rmdir /s /q \"" + dir + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring cmdline = L"cmd.exe " + command;
  if (CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                     nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

bool uninstall(const std::vector<std::wstring>& args) {
  const bool silent = is_silent(args);
  std::wstring install_dir = exe_dir();

  if (!silent) {
    if (show_message(std::wstring(L"确定要卸载 ") + kAppName + L" 吗？\n\n安装位置：" + install_dir +
                         L"\n\n将删除程序文件、开始菜单快捷方式、开机启动项与注册表信息。",
                     MB_ICONQUESTION | MB_YESNO) != IDYES) {
      return false;
    }
  }

  std::wprintf(L"==> 卸载 %s（%s）\n", kAppName, install_dir.c_str());
  kill_running_instances();

  delete_reg_value(HKEY_CURRENT_USER, kRunKey, L"Waybar");
  delete_reg_tree(HKEY_CURRENT_USER, kUninstallKey);
  delete_reg_tree(HKEY_CURRENT_USER, kAppPathsKey);
  remove_start_menu_shortcuts();
  DeleteFileW(join(desktop_dir(), L"Waybar.lnk").c_str());

  // 删除除自身外的所有文件，然后延迟删除自身
  const std::wstring self = join(install_dir, L"uninstall.exe");
  WIN32_FIND_DATAW data{};
  HANDLE handle = FindFirstFileW(join(install_dir, L"*").c_str(), &data);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = data.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring child = join(install_dir, name);
      if (_wcsicmp(child.c_str(), self.c_str()) == 0) continue;
      if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        remove_tree(child);
      } else {
        SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(child.c_str());
      }
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
  }

  schedule_self_delete(install_dir);

  if (!silent) {
    show_message(std::wstring(kAppName) + L" 已卸载。\n\n程序目录将在几秒后自动删除。",
                 MB_ICONINFORMATION | MB_OK);
  }
  std::wprintf(L"==> 卸载完成\n");
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

  // 高 DPI，避免消息框模糊
  SetProcessDPIAware();

  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) {
    show_message(L"初始化 COM 失败，无法创建快捷方式。", MB_ICONERROR | MB_OK);
    return 1;
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

  bool ok = false;
  if (has_flag(args, L"--uninstall") || has_flag(args, L"/uninstall")) {
    ok = uninstall(args);
  } else {
    ok = install(args);
  }

  CoUninitialize();
  return ok ? 0 : 1;
}
