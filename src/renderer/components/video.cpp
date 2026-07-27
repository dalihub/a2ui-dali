#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderVideo(const ComponentModel& comp, DataContext& ctx)
{
  // dali-ui has no native video surface — render a media-frame look: a dark
  // frame with a centred play glyph (conveys "video here" without a real player).
  FlexLayout frame = FlexLayout::New();
  frame.SetDirection(FlexDirection::COLUMN);
  frame.SetJustifyContent(FlexJustify::CENTER);
  frame.SetAlignItems(FlexAlign::CENTER);
  frame.SetRequestedWidth(MATCH_PARENT);
  frame.SetRequestedHeight(Metrics::Dp(200));  // dp-scaled (raw literals halve under 2x capture)
  frame.SetBackgroundColor(UiColor(0x202024));
  frame.SetCornerRadius(Metrics::RadiusCard());
  frame.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(6)), static_cast<uint16_t>(Metrics::Dp(6))));

  // `posterUrl` (v1.0) is the still shown before playback. It goes on as the frame's
  // BACKGROUND rather than a child, so the play glyph keeps sitting on top of it — added
  // as a child it would take its own slot in the flex column and push the glyph out.
  // Absent (every v0.9 payload), the frame is unchanged.
  const TreeNode* posterNode = comp.rawNode->Find("posterUrl");
  std::string     posterUrl  = posterNode ? ResolveString(posterNode, ctx) : std::string();
  if(!posterUrl.empty())
  {
    std::string posterPath = posterUrl;
    if(posterUrl.find("://") == std::string::npos)
    {
      posterPath = (posterUrl[0] == '/') ? posterUrl : (mImageDir + posterUrl);
      std::ifstream pf(posterPath);
      if(!pf.is_open()) posterPath.clear();
    }
    if(!posterPath.empty())
    {
      frame.SetBackgroundImage(posterPath.c_str());
    }
  }

  std::string playPath = mImageDir + "icons/play.png";
  std::ifstream f(playPath);
  if(f.is_open())
  {
    f.close();
    ImageView play = ImageView::New(playPath.c_str());
    play.SetRequestedWidth(Metrics::Dp(48));
    play.SetRequestedHeight(Metrics::Dp(48));
    play.SetImageColor(UiColor(0xFFFFFF));
    frame.Add(play);
  }
  return frame;
}
} // namespace A2ui
