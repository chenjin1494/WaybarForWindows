#include "util/css_reload_helper.hpp"

#include <spdlog/spdlog.h>
#ifndef _WIN32
#include <poll.h>
#ifndef __OpenBSD__
#include <sys/inotify.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#endif
#endif

#include <filesystem>
#include <fstream>
#include <regex>
#include <unordered_map>

#include "config.hpp"
#include "giomm/file.h"
#include "glibmm/refptr.h"

#include "util/fs.hpp"

namespace {
const std::regex IMPORT_REGEX(R"(@import\s+(?:url\()?(?:"|')([^"')]+)(?:"|')\)?;)");
}

waybar::CssReloadHelper::CssReloadHelper(std::string cssFile,
                                         std::function<void(const std::string&)> callback)
    : m_cssFile(std::move(cssFile)), m_callback(std::move(callback)) {}

std::string waybar::CssReloadHelper::getFileContents(const std::string& filename) {
  if (filename.empty()) {
    return {};
  }

  auto file = waybar::util::open_ifstream(filename);
  if (!file.is_open()) {
    return {};
  }

  return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}

std::string waybar::CssReloadHelper::findPath(const std::string& filename) {
  // try path and fallback to looking relative to the config
  std::string result;
  if (waybar::util::path_exists(filename)) {
    result = filename;
  } else {
    result = Config::findConfigPath({filename}).value_or("");
  }

  // File monitor does not work with symlinks, so resolve them
  std::string original = result;
  while (waybar::util::is_symlink_path(result)) {
    result = waybar::util::read_symlink_path(result);

    // prevent infinite cycle
    if (result == original) {
      break;
    }
  }

  return result;
}

void waybar::CssReloadHelper::monitorChanges() {
  auto files = parseImports(m_cssFile);
  for (const auto& file : files) {
    auto gioFile = Gio::File::create_for_path(file);
    if (!gioFile) {
      spdlog::error("Failed to create file for path: {}", file);
      continue;
    }

    auto fileMonitor = gioFile->monitor_file();
    if (!fileMonitor) {
      spdlog::error("Failed to create file monitor for path: {}", file);
      continue;
    }

    auto connection = fileMonitor->signal_changed().connect(
        sigc::mem_fun(*this, &CssReloadHelper::handleFileChange));

    if (!connection.connected()) {
      spdlog::error("Failed to connect to file monitor for path: {}", file);
      continue;
    }
    m_fileMonitors.emplace_back(std::move(fileMonitor));
  }
}

void waybar::CssReloadHelper::changeCssFile(const std::string& newCssFile) {
  m_fileMonitors.clear();
  m_cssFile = newCssFile;
  monitorChanges();
}

void waybar::CssReloadHelper::handleFileChange(Glib::RefPtr<Gio::File> const& file,
                                               Glib::RefPtr<Gio::File> const& other_type,
                                               Gio::FileMonitorEvent event_type) {
  // Multiple events are fired on file changed (attributes, write, changes done hint, etc.), only
  // fire for one
  if (event_type == Gio::FileMonitorEvent::FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
    spdlog::debug("Reloading style, file changed: {}", file->get_path());
    m_callback(m_cssFile);
  }
}

std::vector<std::string> waybar::CssReloadHelper::parseImports(const std::string& cssFile) {
  std::unordered_map<std::string, bool> imports;

  auto cssFullPath = findPath(cssFile);
  if (cssFullPath.empty()) {
    spdlog::error("Failed to find css file: {}", cssFile);
    return {};
  }

  spdlog::debug("Parsing imports for file: {}", cssFullPath);
  imports[cssFullPath] = false;

  auto previousSize = 0UL;
  auto maxIterations = 100U;
  do {
    previousSize = imports.size();
    std::vector<std::string> to_parse;
    for (const auto& [file, parsed] : imports) {
      if (!parsed) {
        to_parse.push_back(file);
      }
    }
    for (const auto& file : to_parse) {
      parseImports(file, imports);
    }

  } while (imports.size() > previousSize && maxIterations-- > 0);

  std::vector<std::string> result;
  for (const auto& [file, parsed] : imports) {
    if (parsed) {
      spdlog::debug("Adding file to watch list: {}", file);
      result.push_back(file);
    }
  }

  return result;
}

void waybar::CssReloadHelper::parseImports(const std::string& cssFile,
                                           std::unordered_map<std::string, bool>& imports) {
  // if the file has already been parsed, skip
  if (imports.find(cssFile) != imports.end() && imports[cssFile]) {
    return;
  }

  auto contents = getFileContents(cssFile);
  std::smatch matches;
  while (std::regex_search(contents, matches, IMPORT_REGEX)) {
    auto importFile = findPath({matches[1].str()});
    if (!importFile.empty() && imports.find(importFile) == imports.end()) {
      imports[importFile] = false;
    }

    contents = matches.suffix().str();
  }

  imports[cssFile] = true;
}
