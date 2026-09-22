#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderView(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  float width = GetNodeFloat(*comp.rawNode, "width", 24.0f);
  float height = GetNodeFloat(*comp.rawNode, "height", 24.0f);
  View view = View::New();
  view.SetRequestedWidth(width);
  view.SetRequestedHeight(height);

  const char* color = GetNodeString(*comp.rawNode, "color", "");
  if(color[0] != '\0')
  {
    view.SetBackgroundColor(ParseHexColor(color));
  }

  Animation pulse = Animation::New(1.0f);
  pulse.AnimateTo(Property(view, Actor::Property::OPACITY), 0.4f);
  pulse.SetLooping(true);
  pulse.SetLoopingMode(Animation::AUTO_REVERSE);
  pulse.Play();
  return view;
}
} // namespace A2ui
