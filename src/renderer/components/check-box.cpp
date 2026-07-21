#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderCheckBox(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  FlexLayout row = FlexLayout::New();
  row.SetDirection(FlexDirection::ROW);
  row.SetAlignItems(FlexAlign::CENTER);
  row.SetRequestedWidth(MATCH_PARENT);
  row.SetRequestedHeight(WRAP_CONTENT);
  row.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(4)), static_cast<uint16_t>(Metrics::Dp(4))));

  // Check box: an outlined white square (web composer) that fills with the accent colour
  // when checked — the border keeps the unchecked box visible instead of a faint fill.
  View checkIcon = View::New();
  checkIcon.SetRequestedWidth(Metrics::Dp(20));
  checkIcon.SetRequestedHeight(Metrics::Dp(20));
  checkIcon.SetCornerRadius(Metrics::RadiusCheck());
  checkIcon.SetBorderlineWidth(Metrics::BorderInput());
  checkIcon.SetBorderlineColor(A2uiTheme::Color("Outline"));

  const TreeNode* valueNode = comp.rawNode->Find("value");
  std::string boundPath = valueNode ? GetBoundPath(valueNode, ctx) : "";

  bool checked = !boundPath.empty() ? ctx.GetDataModel().GetBool(boundPath, false) : false;
  checkIcon.SetBackgroundColor(checked ? COLOR_CHECK_ON : COLOR_CARD_BG);

  // Label
  const char* labelText = GetNodeString(*comp.rawNode, "label", "");
  Label label = Label::New(labelText);
  label.SetFontSize(Metrics::FontChoice());
  label.SetTextColor(COLOR_TEXT_DEFAULT);
  label.SetRequestedWidth(WRAP_CONTENT);
  label.SetRequestedHeight(WRAP_CONTENT);
  label.SetMargin(Extents(static_cast<uint16_t>(Metrics::Dp(8)), 0, 0, 0));

  row.Add(checkIcon);
  row.Add(label);

  // Click toggle
  if(!boundPath.empty())
  {
    // dali-ui 2.5.26: AsInteractive() now returns the InteractiveTrait directly
    // (no lambda configurator). Attach the click handler to the returned trait.
    InteractiveTrait trait = row.AsInteractive();
    trait.ClickedSignal().Connect(this,
      [boundPath, ctx](View, const InputEvent&) mutable -> bool {
        bool current = ctx.GetDataModel().GetBool(boundPath, false);
        ctx.GetDataModel().SetBoolValue(boundPath, !current);
        return true;
      });

  }

  // Watch for data changes → update icon. Re-evaluates the binding, so a checkbox bound to
  // a predicate call ({"call":"not", …}) tracks the call's result, not the raw path value.
  WatchBinding(valueNode, ctx, [checkIcon](const std::string& val) mutable {
    bool on = (val == "true");
    checkIcon.SetBackgroundColor(on ? COLOR_CHECK_ON : COLOR_CARD_BG);
  });

  return row;
}
} // namespace A2ui
