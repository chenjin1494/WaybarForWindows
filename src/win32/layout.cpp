#include "win32/layout.hpp"

#include <algorithm>

namespace waybar::win32 {

Edge parse_edge(const std::string& position) {
  if (position == "bottom") return Edge::Bottom;
  if (position == "left") return Edge::Left;
  if (position == "right") return Edge::Right;
  return Edge::Top;
}

bool is_vertical(Edge edge) { return edge == Edge::Left || edge == Edge::Right; }

LayoutResult compute_layout(const MonitorInfo& monitor, const LayoutOptions& options,
                            int content_width, int content_height) {
  LayoutResult result;
  result.edge = parse_edge(options.position);
  result.vertical = is_vertical(result.edge);

  const Rect& geo = monitor.geometry;
  const int margin_top = std::max(0, options.margin_top);
  const int margin_right = std::max(0, options.margin_right);
  const int margin_bottom = std::max(0, options.margin_bottom);
  const int margin_left = std::max(0, options.margin_left);

  const int configured_width = static_cast<int>(options.width);
  const int configured_height = static_cast<int>(options.height);

  if (!result.vertical) {
    // 高度：显式配置优先，否则用内容高度（至少 1）
    result.height = configured_height > 1 ? configured_height : std::max(1, content_height);

    // 宽度：显式配置则居中；否则铺满（扣除左右 margin）
    if (configured_width > 1) {
      result.width = configured_width;
      result.x = geo.x + (geo.width - result.width) / 2;
    } else {
      result.width = std::max(1, geo.width - margin_left - margin_right);
      result.x = geo.x + margin_left;
    }

    if (result.edge == Edge::Bottom) {
      result.y = geo.y + geo.height - result.height - margin_bottom;
    } else {
      result.y = geo.y + margin_top;
    }
  } else {
    // 宽度：显式配置优先，否则用内容宽度
    result.width = configured_width > 1 ? configured_width : std::max(1, content_width);

    // 高度：显式配置则居中；否则铺满（扣除上下 margin）
    if (configured_height > 1) {
      result.height = configured_height;
      result.y = geo.y + (geo.height - result.height) / 2;
    } else {
      result.height = std::max(1, geo.height - margin_top - margin_bottom);
      result.y = geo.y + margin_top;
    }

    if (result.edge == Edge::Right) {
      result.x = geo.x + geo.width - result.width - margin_right;
    } else {
      result.x = geo.x + margin_left;
    }
  }

  // 夹到显示器范围内，避免负值或溢出
  result.width = std::max(1, result.width);
  result.height = std::max(1, result.height);
  if (result.width > geo.width) result.width = geo.width;
  if (result.height > geo.height) result.height = geo.height;

  return result;
}

}  // namespace waybar::win32
