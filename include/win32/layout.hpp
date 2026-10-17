#pragma once

// bar 几何计算（纯函数，便于单元测试）。
//
// 语义对齐 wlr-layer-shell 的常见用法：
//   - 水平 bar（top/bottom）默认铺满显示器宽度；显式 width 时在显示器内水平居中
//   - 垂直 bar（left/right）默认铺满显示器高度；显式 height 时在显示器内垂直居中
//   - height/width 为 0（自动）时使用内容尺寸
//   - margin 作用于贴边方向（top: +margin-top，bottom: -margin-bottom，left: +margin-left，
//     right: -margin-right）

#include <cstdint>
#include <string>

namespace waybar::win32 {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct MonitorInfo {
  Rect geometry;
  Rect workarea;
  int scale = 1;
  std::string name;
  std::string identifier;
  bool primary = false;
};

enum class Edge { Top, Bottom, Left, Right };

struct LayoutOptions {
  std::string position = "top";
  uint32_t width = 0;
  uint32_t height = 0;
  int margin_top = 0;
  int margin_right = 0;
  int margin_bottom = 0;
  int margin_left = 0;
};

struct LayoutResult {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool vertical = false;
  Edge edge = Edge::Top;
};

Edge parse_edge(const std::string& position);
bool is_vertical(Edge edge);

// content_* 为 GTK 测得的自然尺寸（仅当 width/height 配置为 0 时使用）。
LayoutResult compute_layout(const MonitorInfo& monitor, const LayoutOptions& options,
                            int content_width, int content_height);

}  // namespace waybar::win32
