#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderButton(const ComponentModel& comp,
                                const SurfaceComponentsModel& components,
                                DataContext& ctx)
{
  // Live-composer styles every button as a white outlined pill (default/primary alike);
  // only `borderless` drops the fill+border to a text-only button. The A2UI default
  // variant is "default".
  const char* variant = comp.rawNode ? GetNodeString(*comp.rawNode, "variant", "default") : "default";
  bool isBorderless = (strcmp(variant, "borderless") == 0);

  // Single FlexLayout — the previous nested (borderWrap + button) structure
  // collapsed to zero under DALi's column-align:stretch when the child label
  // was WRAP_CONTENT. Fixed size + a single container avoids that entirely.
  const float kBtnHeight = Metrics::ButtonHeight();       // 40 (dp-scaled pill)
  const float kBtnMinWidth = Metrics::ButtonMinWidth();   // 140
  const float kCharWidth14pt = Metrics::ButtonCharWidth();// glyph advance at the button font
  const float kLabelPadding = Metrics::ButtonLabelPad();  // cushion each side inside the button
  const float    kPadXf = Metrics::Dp(12);   // horizontal padding each side (web ≈ 12px)
  const uint16_t kPadX = static_cast<uint16_t>(kPadXf);
  const uint16_t kPadY = static_cast<uint16_t>(Metrics::Dp(8));

  FlexLayout button = FlexLayout::New();
  button.SetDirection(FlexDirection::ROW);
  button.SetJustifyContent(FlexJustify::CENTER);
  button.SetAlignItems(FlexAlign::CENTER);
  button.SetRequestedHeight(kBtnHeight);
  button.SetMinimumHeight(kBtnHeight);
  button.SetMaximumHeight(kBtnHeight);  // pin the height so every button is uniform
  button.SetCornerRadius(kBtnHeight * 0.5f);  // OneUI CornerRadius 0.5 → pill
  button.SetPadding(Extents(kPadX, kPadX, kPadY, kPadY));
  // No self-margin: the container's gap handles spacing. (A self-margin was overwritten
  // on the 2nd+ row child by the gap logic, making buttons in a Row look uneven.)
  // Don't let the parent Column's align:stretch override our fixed size.
  button.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::CENTER));

  // White outlined pill: white fill, light outline, near-black label. borderless = text-only.
  UiColor bgColor = COLOR_CARD_BG;       // white surface
  UiColor fgColor = COLOR_TEXT_DEFAULT;  // near-black label
  bool outlined = true;
  if(isBorderless)
  {
    bgColor = UiColor(0.0f, 0.0f, 0.0f, 0.0f);
    outlined = false;
  }
  button.SetBackgroundColor(bgColor);
  if(outlined)
  {
    button.SetBorderlineWidth(Metrics::BorderInput());      // 1 dp
    button.SetBorderlineColor(COLOR_BTN_BORDER);            // Outline #e5e5e5
  }

  float btnWidth = kBtnMinWidth;
  if(!comp.childId.empty())
  {
    View childView = RenderComponent(comp.childId, components, ctx);
    Label labelChild = Label::DownCast(childView);
    if(labelChild)
    {
      labelChild.SetTextColor(fgColor);
      labelChild.SetFontWeight(Text::FontWeight::SEMI_BOLD);  // web button labels read semibold (5-agent consensus)
      labelChild.SetFontSize(Metrics::FontButton());
      labelChild.SetHorizontalTextAlignment(Text::Alignment::CENTER);
      // Grow the label box to fit the full glyph run so text like "Submit
      // Reservation" isn't ellipsized. Button width tracks the label so the
      // padding is uniform on both sides regardless of text length.
      Dali::String text = labelChild.GetText();
      std::size_t byteLen = text.Size();
      labelChild.SetMultiLine(false);
      labelChild.SetRequestedHeight(Metrics::Dp(20));
      // Only an actual single glyph / emoji / symbol gets the fixed square; short ASCII
      // words like "Yes"/"No"/"OK" must be sized as text or they clip to a stub ("—").
      const char* tbytes = text.CStr();
      bool hasNonAscii = false;
      for(std::size_t i = 0; i < byteLen; ++i)
        if(static_cast<unsigned char>(tbytes[i]) >= 0x80) { hasNonAscii = true; break; }
      bool isGlyphButton = (byteLen <= 4 && (hasNonAscii || byteLen <= 1));
      if(isGlyphButton)
      {
        // Single glyph / icon / emoji button → a fixed square so a row of them is
        // uniform (e.g. music-player prev/play/next); the pill radius = a circle.
        labelChild.SetRequestedWidth(kBtnHeight);
        btnWidth = kBtnHeight;
      }
      else
      {
        // Text button → size to content with a small floor (the reference renderer has no
        // large MinimumWidth) so e.g. "Sign up" stays snug instead of a wide pill.
        float labelWidth = static_cast<float>(byteLen) * kCharWidth14pt + kLabelPadding * 2.0f;
        labelChild.SetRequestedWidth(labelWidth);
        // The button MUST be the label box + its OWN left/right padding, or the padding eats into
        // the label's content area and clips the text (a semibold "Purchase" → "Purcha…"; the
        // previous +Dp(16) was only HALF the 2×Dp(12) padding). 2×kPadXf fits the label exactly;
        // floor 64dp keeps tiny labels a sane pill width.
        btnWidth = std::max(Metrics::Dp(64), labelWidth + 2.0f * kPadXf);
      }  // dp padding each side
    }
    else
    {
      // Icon-only button (e.g. a music-player prev/play/next transport control): the web
      // draws these as a COMPACT borderless circular tap target, not a 140dp outlined pill.
      btnWidth = kBtnHeight;
      button.SetBackgroundColor(UiColor(0.0f, 0.0f, 0.0f, 0.0f));
      button.SetBorderlineWidth(0.0f);
      // The icon is an ACTION control, not an inline decorative glyph — override the icon's
      // muted default to the button's dark foreground and enlarge it (the web transport
      // glyphs are ~24px dark, not a faint 18px grey mark). Without this the prev/play/next
      // controls render as barely-visible specks.
      ImageView iconChild = ImageView::DownCast(childView);
      if(iconChild)
      {
        iconChild.SetImageColor(fgColor);
        iconChild.SetRequestedWidth(Metrics::Dp(24));
        iconChild.SetRequestedHeight(Metrics::Dp(24));
      }
    }
    button.Add(childView);
  }
  button.SetRequestedWidth(btnWidth);

  // Action handling
  //
  // Note: dali-ui has no native Button — this FlexLayout *is* the button.
  // We attach an independent TapGestureDetector instead of relying on
  // InteractiveTrait or TouchedSignal, because neither fires reliably for
  // Button layouts nested inside a ScrollView/List ancestor (the parent's
  // pan detector ends up consuming the touch sequence before our view's
  // click signal can be synthesized). The detector handle must outlive the
  // view, so we park it in mTapDetectors.
  if(comp.rawNode)
  {
    const TreeNode* actionNode = comp.rawNode->Find("action");
    if(actionNode)
    {
      std::string compId = comp.id;
      DataContext capturedCtx = ctx;

      // A Button's own `checks` disable it: while any rule fails the button is greyed out
      // and neither input path may fire the action.
      std::shared_ptr<bool> enabled = SetupActionGate(comp, ctx, button);

      Dali::TapGestureDetector detector = Dali::TapGestureDetector::New();
      detector.Attach(button);
      detector.DetectedSignal().Connect(this,
        [this, actionNode, compId, capturedCtx, enabled](
          Dali::Actor, const Dali::TapGesture&) mutable {
          if(!*enabled) return;
          mActionDispatcher.Dispatch(*actionNode, compId, capturedCtx);
        });
      RetainTapDetector(detector);

      // TV remote: make the button focusable and dispatch the same action on OK/Enter.
      EnableKeyActivation(button, [this, actionNode, compId, capturedCtx, enabled]() {
        if(!*enabled) return;
        mActionDispatcher.Dispatch(*actionNode, compId, capturedCtx);
      });
    }
  }

  return button;
}
} // namespace A2ui
