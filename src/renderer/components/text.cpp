#include "renderer/render-internal.h"

namespace A2ui
{

namespace
{
// dali-ui has no markdown renderer; the web composer renders Text content as markdown.
// Strip the inline/structural markers that would otherwise show as literal characters
// (a leading "# " heading prefix, **bold**/_em_ wrappers, `code` ticks) so the visible
// text matches the web instead of leaking raw markdown (e.g. a stray "#").
std::string StripMarkdown(const std::string& in)
{
  std::string s = in;
  // Leading ATX heading markers: "#", "##" … followed by space(s).
  std::size_t p = 0;
  while(p < s.size() && s[p] == '#') ++p;
  if(p > 0 && p <= 6 && p < s.size() && s[p] == ' ')
  {
    while(p < s.size() && s[p] == ' ') ++p;
    s = s.substr(p);
  }
  // Inline emphasis/code markers: remove the marker runs, keep the wrapped text.
  std::string out;
  out.reserve(s.size());
  for(std::size_t i = 0; i < s.size();)
  {
    if(s[i] == '*' || s[i] == '_')          // * ** _ __  emphasis
    {
      char m = s[i];
      while(i < s.size() && s[i] == m) ++i;  // skip the whole run
      continue;
    }
    if(s[i] == '`') { ++i; continue; }       // `code`
    out += s[i++];
  }
  return out;
}

// Size a Label to its REAL wrapped line count at content width `width`, replacing the
// pre-layout character estimate once the true geometry (or a newly-arrived bound value) is
// known. Guarded twice, per the View::LayoutFinishedSignal contract:
//   (1) skip while the text has no laid-out lines yet (empty, or a data-bound value that has
//       not arrived) so we never collapse the one-line slot RenderText reserved for it — the
//       raw `lineH * GetLineCount()` would resolve to 0 and hide the row;
//   (2) only write the height when it actually differs, so re-running this inside the settle
//       signal cannot spin the endless dirty->settled->emit loop the API warns about.
void FitLabelHeightToLines(Label label, float width)
{
  if(width <= 0.0f) return;                 // not laid out yet — the reserved height stands
  int lines = label.GetLineCount(width);
  if(lines < 1) return;                     // empty text — keep the reserved slot, don't collapse
  float target = Metrics::LineHeight(label.GetFontSize()) * lines;
  float diff   = label.GetRequestedHeight() - target;
  if(diff < 0.0f) diff = -diff;
  if(diff > 0.5f) label.SetRequestedHeight(target);
}
} // namespace

View A2uiRenderer::RenderText(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  const TreeNode* textNode = comp.rawNode->Find("text");
  std::string text = textNode ? ResolveString(textNode, ctx) : "";
  text = StripMarkdown(text);

  const char* variant = GetNodeString(*comp.rawNode, "variant", "body");
  float fontSize = VariantToFontSize(variant);

  // A value with NO breakable whitespace (a currency/number cell "$43,500.25", a single word)
  // must NEVER wrap — the web renders these nowrap on one line. Wrapping such a token (e.g. in
  // a narrow weighted grid column) split "$43,500.25" across two lines and inflated the row.
  bool wrappable = (text.find(' ') != std::string::npos);

  Label label = Label::New(text.c_str());
  label.SetFontSize(fontSize);
  label.SetRequestedWidth(MATCH_PARENT);
  label.SetTextColor(COLOR_TEXT_DEFAULT);
  label.SetMultiLine(wrappable);
  label.SetLineWrapMode(Text::LineWrapMode::WORD);

  // DALi Label inside a flex-grown parent sometimes measures height=0 when the parent's
  // resolved width is only known after the layout pass, so we reserve a height estimate.
  // Estimate chars-per-line from the font size against the card content width, and use the
  // web type scale's line height per line so wrapped copy occupies the same vertical space
  // as the web renderer. All terms are dp-scaled, so the estimate holds at any capture DPI.
  float lineH = Metrics::LineHeight(fontSize);
  if(!text.empty())
  {
    // Available width: the narrow-column budget if we're inside a Row's column, else the full
    // card content width. 0.58 (avg glyph advance / font size) is deliberately a touch wide so
    // the line count rounds UP rather than down — under-counting clips the wrap (the visible
    // bug), over-counting only adds a little slack, and the cards currently run short anyway.
    float availW = (mTextWidthBudget > 0.0f) ? mTextWidthBudget : Metrics::CardContentWidth();
    // Safety margin: the budget is an ESTIMATE, and a Row's leading image/sibling can overshoot
    // its own width estimate, leaving the text column a touch narrower than `availW` at render
    // time. Shave the budget ~8% so a boundary-length name (h4 "Wireless Headphones Pro") reserves
    // the 2 lines it actually wraps to instead of 1 and getting ellipsized. Only adds height slack.
    availW *= 0.92f;
    int charsPerLine = static_cast<int>(availW / (fontSize * 0.58f));
    if(charsPerLine < 6) charsPerLine = 6;
    int estLines = wrappable ? static_cast<int>((text.size() + charsPerLine - 1) / charsPerLine) : 1;
    if(estLines < 1) estLines = 1;
    if(estLines > 20) estLines = 20;
    label.SetRequestedHeight(lineH * estLines);
  }
  else
  {
    // Data-bound text is usually empty at first paint (its value arrives in a later
    // updateDataModel); reserve one line so the flex keeps the slot and the watch's
    // update becomes visible instead of the row collapsing the value away.
    bool dataBound = textNode && textNode->GetType() == TreeNode::OBJECT;
    label.SetRequestedHeight(dataBound ? lineH : WRAP_CONTENT);
  }

  // Once layout settles, correct the height to the real wrapped line count (the estimate above
  // is only a first-paint reservation). Use the arranged width the signal hands us; the guards
  // live in FitLabelHeightToLines so an empty slot is never collapsed and the settle loop never
  // spins.
  label.LayoutFinishedSignal().Connect(this, [](View view, LayoutRect rect) {
    FitLabelHeightToLines(Label::DownCast(view), rect.width);
  });

  // Caption/body color hints
  if(strcmp(variant, "caption") == 0)
  {
    label.SetTextColor(COLOR_TEXT_MUTED);
  }

  const char* color = GetNodeString(*comp.rawNode, "color", "");
  if(color[0] != '\0')
  {
    label.SetTextColor(ParseHexColor(color));
  }

  // Font weight (bold)
  const char* fontWeight = GetNodeString(*comp.rawNode, "fontWeight", "");
  if(fontWeight[0] != '\0')
  {
    if(strcmp(fontWeight, "bold") == 0)
      label.SetFontWeight(Text::FontWeight::BOLD);
    else if(strcmp(fontWeight, "semiBold") == 0)
      label.SetFontWeight(Text::FontWeight::SEMI_BOLD);
    else if(strcmp(fontWeight, "light") == 0)
      label.SetFontWeight(Text::FontWeight::LIGHT);
    else if(strcmp(fontWeight, "medium") == 0)
      label.SetFontWeight(Text::FontWeight::MEDIUM);
  }
  else if(strcmp(variant, "h1") == 0 || strcmp(variant, "h2") == 0 || strcmp(variant, "h3") == 0)
  {
    // Headings are SEMI-BOLD (600). Frame-aligned comparison showed a full BOLD (700) heading
    // reads clearly heavier than the web's title weight across many cards (Purchase License,
    // Today's Steps, Product Launch Meeting…); 600 matches.
    label.SetFontWeight(Text::FontWeight::SEMI_BOLD);
  }
  else if(strcmp(variant, "h4") == 0)
  {
    // h4 (body-large) is a sub-heading — semi-bold, lighter than the h1-h3 titles.
    label.SetFontWeight(Text::FontWeight::SEMI_BOLD);
  }

  // Font slant (italic)
  const char* fontStyle = GetNodeString(*comp.rawNode, "fontStyle", "");
  if(strcmp(fontStyle, "italic") == 0)
  {
    label.SetFontSlant(Text::FontSlant::ITALIC);
  }

  // Underline
  const TreeNode* underlineNode = comp.rawNode->Find("underline");
  if(underlineNode)
  {
    Text::Underline underline;
    underline.SetColor(label.GetTextColor());
    if(underlineNode->GetType() == TreeNode::OBJECT)
    {
      const char* ulColor = GetNodeString(*underlineNode, "color", "");
      if(ulColor[0] != '\0') underline.SetColor(ParseHexColor(ulColor));
      float thickness = GetNodeFloat(*underlineNode, "thickness", 1.0f);
      underline.SetThickness(thickness);
    }
    label.SetTextUnderline(underline);
  }

  // Text alignment
  const char* align = GetNodeString(*comp.rawNode, "align", "");
  if(strcmp(align, "center") == 0)
    label.SetHorizontalTextAlignment(Text::Alignment::CENTER);
  else if(strcmp(align, "end") == 0)
    label.SetHorizontalTextAlignment(Text::Alignment::END);

  // The web composer's structured Text components carry layout-m-0 (zero margin) — all the
  // inter-element spacing comes from the parent Column/Row gap, NOT from per-element margins.
  // (Earlier this added 5-12px per element on top of the gap, doubling every vertical seam.)
  label.SetMargin(Extents(0, 0, 0, 0));

  // Reactive data binding: re-run the binding (path OR function call) on every change
  // of anything it reads, so a streamed value lands here formatted exactly as it was on
  // the first paint above.
  if(textNode && textNode->GetType() == TreeNode::OBJECT)
  {
    WatchBinding(textNode, ctx, [label](const std::string& val) mutable {
      // Strip markdown on update too, matching the initial paint (a streamed value may
      // carry **bold** / "# " markers that would otherwise render literally).
      std::string shown = StripMarkdown(val);
      label.SetText(Dali::String(shown.c_str()));
      // The multi-line / wrap decision was made at first paint from the (usually empty)
      // initial value, so a data-bound label is single-line by default. Recompute it from the
      // arrived value so a multi-word streamed value actually wraps — while a single unbroken
      // token (a number/currency cell) still stays on one line, matching the initial-paint rule.
      label.SetMultiLine(shown.find(' ') != std::string::npos);
      // A value arriving AFTER the first layout settled cannot rely on LayoutFinishedSignal:
      // SetText only re-runs measure when the height is WRAP_CONTENT, but this label's height
      // is pinned to a concrete value, so the signal never re-fires and the row would keep its
      // reserved (or collapsed) height, clipping/hiding the streamed text. Re-fit here — the
      // height change itself invalidates measure and re-arranges the label at full height.
      FitLabelHeightToLines(label, label.GetSize().width);
    });
  }

  return label;
}
} // namespace A2ui
