#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderSlider(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  FlexLayout container = FlexLayout::New();
  container.SetDirection(FlexDirection::COLUMN);
  container.SetRequestedWidth(MATCH_PARENT);
  container.SetRequestedHeight(WRAP_CONTENT);
  container.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(4)), static_cast<uint16_t>(Metrics::Dp(4))));

  // Label (optional)
  const char* labelText = GetNodeString(*comp.rawNode, "label", "");
  if(labelText[0] != '\0')
  {
    Label label = Label::New(labelText);
    label.SetFontSize(Metrics::FontLabel());
    label.SetTextColor(A2uiTheme::Color("OnSurfaceContainerLow"));
    label.SetRequestedWidth(MATCH_PARENT);
    label.SetRequestedHeight(WRAP_CONTENT);
    container.Add(label);
  }

  float minVal = GetNodeFloat(*comp.rawNode, "min", 0.0f);
  float maxVal = GetNodeFloat(*comp.rawNode, "max", 100.0f);

  const TreeNode* valueNode = comp.rawNode->Find("value");
  std::string boundPath = valueNode ? GetBoundPath(valueNode, ctx) : "";

  float currentVal = !boundPath.empty() ? ctx.GetDataModel().GetFloat(boundPath, minVal) : minVal;

  // Value display — resolve the binding itself (not the raw value at its path), so a
  // FunctionCall-valued slider paints its formatted result and matches what the watch
  // below will write. A plain {path} resolves to the data model's own formatting, which
  // prints the bound number the way the web does (decimals preserved: 0.45, not 0).
  std::string valDisp = valueNode ? ResolveString(valueNode, ctx) : "";
  if(valDisp.empty()) { std::ostringstream o; o << currentVal; valDisp = o.str(); }
  Label valueLabel = Label::New(valDisp.c_str());
  valueLabel.SetFontSize(Metrics::FontButton());
  valueLabel.SetTextColor(COLOR_TEXT_DEFAULT);
  valueLabel.SetRequestedWidth(WRAP_CONTENT);
  valueLabel.SetRequestedHeight(WRAP_CONTENT);
  valueLabel.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(2)), static_cast<uint16_t>(Metrics::Dp(4))));

  // Thin track: filled portion + thumb + empty portion (web shows a single hairline track
  // with a round thumb — no +/- steppers).
  FlexLayout trackRow = FlexLayout::New();
  trackRow.SetDirection(FlexDirection::ROW);
  trackRow.SetAlignItems(FlexAlign::CENTER);
  trackRow.SetRequestedWidth(MATCH_PARENT);
  trackRow.SetRequestedHeight(Metrics::Dp(20));

  float ratio = (maxVal > minVal) ? (currentVal - minVal) / (maxVal - minVal) : 0.0f;

  View fillTrack = View::New();
  fillTrack.SetRequestedHeight(Metrics::Dp(4));
  fillTrack.SetBackgroundColor(COLOR_SLIDER_FILL);
  fillTrack.SetCornerRadius(Metrics::RadiusSliderTrack());
  fillTrack.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(ratio > 0.01f ? ratio : 0.01f).SetFlexBasis(0.0f));

  View thumb = View::New();
  thumb.SetRequestedWidth(Metrics::RadiusSliderThumb() * 2.0f);   // 16dp circle
  thumb.SetRequestedHeight(Metrics::RadiusSliderThumb() * 2.0f);
  thumb.SetCornerRadius(Metrics::RadiusSliderThumb());
  thumb.SetBackgroundColor(COLOR_SLIDER_THUMB);

  View emptyTrack = View::New();
  emptyTrack.SetRequestedHeight(Metrics::Dp(4));
  emptyTrack.SetBackgroundColor(COLOR_SLIDER_TRACK);
  emptyTrack.SetCornerRadius(Metrics::RadiusSliderTrack());
  float emptyRatio = 1.0f - ratio;
  emptyTrack.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(emptyRatio > 0.01f ? emptyRatio : 0.01f).SetFlexBasis(0.0f));

  trackRow.Add(fillTrack);
  trackRow.Add(thumb);
  trackRow.Add(emptyTrack);

  // The numeric value caption ("0.45") is OFF by default — the web slider is a bare track
  // (a music progress bar shows times in a separate row, not on the slider). Only show it when
  // the message opts in via showValue:true.
  const TreeNode* svNode = comp.rawNode ? comp.rawNode->Find("showValue") : nullptr;
  bool showValue = svNode && svNode->GetType() == TreeNode::BOOLEAN && svNode->GetBoolean();
  if(showValue) container.Add(valueLabel);
  container.Add(trackRow);

  // Reactive: update the value label and the fill/empty split when the bound value changes.
  {
    DataContext valCtx = ctx;
    WatchBinding(valueNode, ctx,
      [valueLabel, fillTrack, emptyTrack, minVal, maxVal, valCtx, boundPath](
        const std::string& val) mutable {
        valueLabel.SetText(Dali::String(val.c_str()));
        // The thumb position comes from the NUMBER, not from the label: a formatted
        // display ("1,234") does not parse back to its value.
        float v = 0.0f;
        if(!boundPath.empty())
        {
          v = valCtx.GetDataModel().GetFloat(boundPath, minVal);
        }
        else
        {
          try { v = std::stof(val); } catch(...) {}
        }
        float r = (maxVal > minVal) ? (v - minVal) / (maxVal - minVal) : 0.0f;
        if(r < 0.01f) r = 0.01f;
        if(r > 0.99f) r = 0.99f;
        fillTrack.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(r).SetFlexBasis(0.0f));
        emptyTrack.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f - r).SetFlexBasis(0.0f));
      });
  }

  return container;
}
} // namespace A2ui
