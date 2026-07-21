#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderDateTimeInput(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  FlexLayout container = FlexLayout::New();
  container.SetDirection(FlexDirection::COLUMN);
  container.SetRequestedWidth(MATCH_PARENT);
  container.SetRequestedHeight(WRAP_CONTENT);
  container.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(4)), static_cast<uint16_t>(Metrics::Dp(4))));

  const char* labelText = GetNodeString(*comp.rawNode, "label", "Date/Time");
  Label label = Label::New(labelText);
  label.SetFontSize(Metrics::FontLabel());
  label.SetTextColor(A2uiTheme::Color("OnSurfaceContainerLow"));  // OneUIColor.OnSurfaceContainerLow
  label.SetRequestedWidth(MATCH_PARENT);
  label.SetRequestedHeight(WRAP_CONTENT);
  label.SetMargin(Extents(0, 0, 0, static_cast<uint16_t>(Metrics::Dp(4))));
  container.Add(label);

  // Outlined input box (web composer: white box, 1px border, calendar glyph on the right).
  FlexLayout inputBox = FlexLayout::New();
  inputBox.SetDirection(FlexDirection::ROW);
  inputBox.SetAlignItems(FlexAlign::CENTER);
  inputBox.SetRequestedWidth(MATCH_PARENT);
  inputBox.SetRequestedHeight(Metrics::InputHeight());
  inputBox.SetBackgroundColor(COLOR_INPUT_BG);
  inputBox.SetCornerRadius(Metrics::RadiusInput());
  inputBox.SetBorderlineWidth(Metrics::BorderInput());
  inputBox.SetBorderlineColor(COLOR_INPUT_BORDER);
  inputBox.SetPadding(Extents(static_cast<uint16_t>(Metrics::Dp(12)), static_cast<uint16_t>(Metrics::Dp(12)), 0, 0));

  const TreeNode* valueNode = comp.rawNode->Find("value");
  std::string boundPath = valueNode ? GetBoundPath(valueNode, ctx) : "";

  // Empty native date inputs render a "mm/dd/yyyy, --:-- --" placeholder on the web; a value
  // is shown in the same MM/DD/YYYY style.
  const std::string kPlaceholder = "mm/dd/yyyy, --:-- --";
  auto formatDateTime = [](const std::string& iso) -> std::string {
    if(iso.size() < 10) return iso;
    auto sub = [&](size_t p, size_t n) { return iso.substr(p, n); };
    std::string out = sub(5, 2) + "/" + sub(8, 2) + "/" + sub(0, 4);
    if(iso.size() >= 16)
    {
      int hh = 0; try { hh = std::stoi(sub(11, 2)); } catch(...) {}
      int h12 = hh % 12; if(h12 == 0) h12 = 12;
      out += ", " + std::to_string(h12) + ":" + sub(14, 2) + (hh < 12 ? " AM" : " PM");
    }
    return out;
  };
  // Resolve the binding, not the raw value at its path, so a FunctionCall-valued input
  // paints the same string the watch below will hand it.
  std::string raw = valueNode ? ResolveString(valueNode, ctx) : "";
  std::string displayText = raw.empty() ? kPlaceholder : formatDateTime(raw);

  Label inputLabel = Label::New(displayText.c_str());
  inputLabel.SetFontSize(Metrics::FontInput());
  inputLabel.SetTextColor(raw.empty() ? A2uiTheme::Color("OnSurfaceContainerLow") : COLOR_TEXT_DEFAULT);
  inputLabel.SetRequestedWidth(WRAP_CONTENT);
  inputLabel.SetRequestedHeight(WRAP_CONTENT);
  inputLabel.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f));
  inputBox.Add(inputLabel);

  // Calendar affordance — a MUTED MONOCHROME icon. The old 📅 emoji rendered in full colour,
  // reading as a red blob next to the field; the web shows a small grey picker glyph. Use the
  // shipped calendar glyph tinted to the muted text colour (falls back to no icon if missing).
  std::string calPath = mImageDir + "icons/calendarToday.png";
  std::ifstream calFile(calPath);
  if(calFile.is_open())
  {
    calFile.close();
    ImageView calIcon = ImageView::New(calPath.c_str());
    calIcon.SetRequestedWidth(Metrics::Dp(16));
    calIcon.SetRequestedHeight(Metrics::Dp(16));
    calIcon.SetImageColor(COLOR_TEXT_MUTED);
    calIcon.SetMargin(Extents(static_cast<uint16_t>(Metrics::Dp(6)), static_cast<uint16_t>(Metrics::Dp(8)), 0, 0));
    inputBox.Add(calIcon);
  }

  // Re-evaluates the binding, so the update path formats exactly what the first paint did.
  WatchBinding(valueNode, ctx,
    [inputLabel, formatDateTime, kPlaceholder](const std::string& val) mutable {
      inputLabel.SetText(Dali::String((val.empty() ? kPlaceholder : formatDateTime(val)).c_str()));
      inputLabel.SetTextColor(val.empty() ? A2uiTheme::Color("OnSurfaceContainerLow") : COLOR_TEXT_DEFAULT);
    });

  container.Add(inputBox);

  // Error label
  Label errorLabel = Label::New("");
  errorLabel.SetFontSize(Metrics::FontError());
  errorLabel.SetTextColor(COLOR_ERROR);
  errorLabel.SetRequestedWidth(MATCH_PARENT);
  errorLabel.SetRequestedHeight(WRAP_CONTENT);
  errorLabel.SetProperty(Actor::Property::VISIBLE, false);
  container.Add(errorLabel);

  if(!boundPath.empty())
  {
    SetupChecks(comp, ctx, errorLabel, boundPath);
  }

  return container;
}
} // namespace A2ui
