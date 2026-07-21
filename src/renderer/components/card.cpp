#include "renderer/render-internal.h"

namespace A2ui
{

namespace
{
std::pair<const TreeNode*, std::string>
FindFirstActionInSubtree(const std::string& compId,
                         const SurfaceComponentsModel& components)
{
  const ComponentModel* comp = components.GetComponent(compId);
  if(!comp || !comp->rawNode) return {nullptr, std::string()};

  if(const TreeNode* actionNode = comp->rawNode->Find("action"))
  {
    return {actionNode, compId};
  }

  if(!comp->childId.empty())
  {
    auto r = FindFirstActionInSubtree(comp->childId, components);
    if(r.first) return r;
  }
  for(const auto& cid : comp->childIds)
  {
    auto r = FindFirstActionInSubtree(cid, components);
    if(r.first) return r;
  }
  return {nullptr, std::string()};
}
} // namespace

View A2uiRenderer::RenderCard(const ComponentModel& comp,
                              const SurfaceComponentsModel& components,
                              DataContext& ctx)
{
  FlexLayout card = FlexLayout::New();
  card.SetDirection(FlexDirection::COLUMN);
  card.SetRequestedWidth(MATCH_PARENT);
  card.SetRequestedHeight(WRAP_CONTENT);

  const char* bg = comp.rawNode ? GetNodeString(*comp.rawNode, "background", "") : "";
  card.SetBackgroundColor(bg[0] != '\0' ? ParseHexColor(bg) : COLOR_CARD_BG);
  card.SetCornerRadius(Metrics::RadiusCard());
  card.SetBorderlineWidth(Metrics::BorderCard());
  card.SetBorderlineColor(A2uiTheme::Color("OutlineLow"));  // subtle edge for a white card (web look)

  // Web cards are ELEVATED with a DIRECTIONAL drop shadow: the left/right/top edges show only a
  // hairline, while the BOTTOM carries a soft shadow ~3-4x that thickness. To match, the blurred
  // black quad is pushed DOWN by 6dp (> its 4dp blur) so its top hides behind the card and the
  // blur pools below; the small blur keeps the sides a thin line. alpha 0.20 lands on the web's
  // gentle bottom halo (a wider blur read as an even halo on all sides, which was wrong). Every Card.
  Property::Map shadow;
  shadow.Insert("visualType", "COLOR");
  shadow.Insert("mixColor", Vector4(0.0f, 0.0f, 0.0f, 0.20f));
  shadow.Insert("blurRadius", Metrics::Dp(4.0f));
  shadow.Insert("cornerRadius", Metrics::RadiusCard());
  Property::Map shadowXform;
  shadowXform.Insert("offset", Vector2(0.0f, Metrics::Dp(6.0f)));
  shadowXform.Insert("offsetPolicy", Vector2(1.0f, 1.0f));  // ABSOLUTE world px
  shadowXform.Insert("size", Vector2(1.0f, 1.0f));
  shadowXform.Insert("sizePolicy", Vector2(0.0f, 0.0f));    // RELATIVE — 100% of the card
  shadow.Insert("transform", shadowXform);
  card.SetProperty(View::Property::SHADOW, shadow);

  // Card padding: a message-declared `padding` (logical units) wins, else the web
  // composer's ~24 inset. Both dp-scaled so the inset tracks density / high-DPI capture
  // (a raw uint16 would be only half the intended inset under the 2x capture DPI).
  float padding = comp.rawNode ? GetNodeFloat(*comp.rawNode, "padding", -1.0f) : -1.0f;
  float padPx = (padding >= 0.0f) ? Metrics::Dp(padding) : Metrics::PadCard();
  uint16_t p = static_cast<uint16_t>(padPx);
  card.SetPadding(Extents(p, p, p, p));
  card.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(10)), 0));

  if(!comp.childId.empty())
  {
    View childView = RenderComponent(comp.childId, components, ctx);
    card.Add(childView);
  }

  // NOTE: an explicit bottom-padding SPACER used to live here to work around an old dali-ui that
  // dropped a WRAP_CONTENT column's bottom inset. The current dali-ui DOES apply SetPadding's
  // bottom inset, so the spacer DOUBLE-counted it: measured bottom inset was 54dp vs the web's
  // 27dp (== top), making EVERY card ~24dp too tall with a big gap under the last element. Removed
  // — SetPadding alone now gives the symmetric inset that matches the web.

  // Fallback tap target: if this card contains a descendant with an
  // `action` (typically a Button — dali-ui has no native Button, so the
  // FlexLayout-based button can fail to receive taps inside a List/Scroll
  // ancestor), attach a TapGestureDetector to the card itself and dispatch
  // that action on tap. A separate detector attached to the Button view
  // runs in parallel; DALi's gesture system routes a tap to the innermost
  // attached detector first, so tapping on the Button fires only the
  // Button handler, and tapping elsewhere on the card fires this one.
  auto actionInfo = FindFirstActionInSubtree(comp.id, components);
  if(actionInfo.first)
  {
    const TreeNode* actionNode = actionInfo.first;
    std::string sourceId = actionInfo.second;
    DataContext capturedCtx = ctx;
    Dali::TapGestureDetector detector = Dali::TapGestureDetector::New();
    detector.Attach(card);
    detector.DetectedSignal().Connect(this,
      [this, actionNode, sourceId, capturedCtx](
        Dali::Actor, const Dali::TapGesture&) mutable {
        mActionDispatcher.Dispatch(*actionNode, sourceId, capturedCtx);
      });
    RetainTapDetector(detector);

    // TV remote: a clickable card is a focus target too; OK/Enter runs the same action.
    EnableKeyActivation(card, [this, actionNode, sourceId, capturedCtx]() {
      mActionDispatcher.Dispatch(*actionNode, sourceId, capturedCtx);
    });
  }

  return card;
}
} // namespace A2ui
