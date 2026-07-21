#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderProgressBar(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  const TreeNode* progNode = comp.rawNode->Find("progress");

  // progress may arrive as 0..1 or 0..100, and the data model often stores it as
  // a string ("0.45") — parse defensively and normalise to 0..1.
  auto normalise = [](float p) {
    if(p > 1.0f) p /= 100.0f;
    return std::max(0.0f, std::min(1.0f, p));
  };
  auto parseStr = [normalise](const std::string& s, float fb) {
    float p = fb;
    if(!s.empty()) { try { p = std::stof(s); } catch(...) {} }
    return normalise(p);
  };
  // Resolve the binding (path OR call) for the same reason the watch below re-evaluates
  // it: the first paint and every update must go through identical code.
  float progress = progNode ? parseStr(ResolveString(progNode, ctx),
                                       normalise(ResolveFloat(progNode, ctx, 0.0f)))
                            : 0.0f;

  const float kBarH = Metrics::Dp(6);  // dp-scaled like every other component dimension
  FlexLayout track = FlexLayout::New();
  track.SetDirection(FlexDirection::ROW);
  track.SetRequestedWidth(MATCH_PARENT);
  track.SetRequestedHeight(kBarH);
  track.SetBackgroundColor(COLOR_INPUT_BORDER);  // OneUI Outline track
  track.SetCornerRadius(kBarH * 0.5f);
  track.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(6)), static_cast<uint16_t>(Metrics::Dp(6))));

  View fill = View::New();
  fill.SetRequestedHeight(kBarH);
  fill.SetBackgroundColor(COLOR_PRIMARY);        // OneUI Primary fill
  fill.SetCornerRadius(kBarH * 0.5f);
  fill.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(progress).SetFlexBasis(0.0f));

  View rest = View::New();
  rest.SetRequestedHeight(kBarH);
  rest.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f - progress).SetFlexBasis(0.0f));

  track.Add(fill);
  track.Add(rest);

  {
    WatchBinding(progNode, ctx,
      [fill, rest, parseStr](const std::string& v) mutable {
        float p = parseStr(v, 0.0f);
        fill.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(p).SetFlexBasis(0.0f));
        rest.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f - p).SetFlexBasis(0.0f));
      });
  }

  return track;
}
} // namespace A2ui
