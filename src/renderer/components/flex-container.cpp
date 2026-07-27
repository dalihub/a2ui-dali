#include "renderer/render-internal.h"

namespace A2ui
{

namespace
{
// Count UTF-8 CODEPOINTS (visible glyphs), not bytes. A multibyte run like "★★★★★" is 15 bytes
// but 5 glyphs; sizing a Text's width from the byte count reserved ~3x too much room and shoved
// the trailing sibling far to the right (the product "(2,847 reviews)" flung off after the
// stars). Continuation bytes have the top bits 10xxxxxx, so we skip them.
std::size_t Utf8Len(const char* s, std::size_t bytes)
{
  std::size_t n = 0;
  for(std::size_t i = 0; i < bytes; ++i)
    if((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
  return n;
}

// Estimate a string's natural rendered width (device px) for reserving a trailing label's slot.
// Glyph advance varies hugely: ASCII digits/letters are ~0.6em, but a wide symbol glyph (a ★
// rating star, an emoji) is ~1.0em. Counting every glyph at 0.6em CLIPPED "★★★★★" to "★★★…"
// (looked like a 3-star rating); counting bytes over-reserved 3x (flung the sibling right). So
// weight ASCII lead bytes at 0.6 and non-ASCII glyphs (lead byte >= 0xC0) at 1.0, + 1em cushion.
float TextNaturalWidth(const char* s, std::size_t bytes, float fontSize)
{
  float units = 0.0f;
  for(std::size_t i = 0; i < bytes; ++i)
  {
    unsigned char b = static_cast<unsigned char>(s[i]);
    if((b & 0xC0) == 0x80) continue;       // continuation byte — same glyph
    units += (b >= 0xC0) ? 1.0f : 0.6f;    // wide (multibyte) glyph vs ASCII
  }
  return units * fontSize + fontSize;
}

/**
 * Reserve a SHORT Row label's natural width so a flex-grow sibling cannot squeeze it.
 *
 * A short trailing Label (a line-item price, a value, a duration) sitting AFTER a flex-grow
 * sibling gets its space consumed and clips ("$6.45" → "$6"), so it is pinned to its glyph
 * width and stopped from shrinking. A long label is left to wrap/shrink as before.
 *
 * Split out of the child loop because it has to run more than once: a DATA-BOUND label is
 * EMPTY at first paint (0 glyphs → no reservation), and without re-running this when the
 * streamed value lands, the label keeps the WRAP_CONTENT + shrink slot that was measured
 * for the empty string and the arriving text is squeezed away.
 *
 * @return true if the label was pinned (the caller must then not re-apply flex-shrink).
 */
bool PinShortRowLabel(Label labelChild, float fontSize)
{
  labelChild.SetRequestedWidth(WRAP_CONTENT);
  Dali::String t      = labelChild.GetText();
  std::size_t  glyphs = Utf8Len(t.CStr(), t.Size());   // codepoints, not bytes (★★★★★)
  if(glyphs == 0 || glyphs > 16) return false;
  // Reserve the glyph-weighted natural width so wide ★ glyphs aren't clipped to "★★★…"
  // and ASCII values like "$6.45"/"8:09" aren't over-reserved.
  labelChild.SetRequestedWidth(TextNaturalWidth(t.CStr(), t.Size(), fontSize));
  labelChild.SetLayoutParams(FlexLayoutParams::New().SetFlexShrink(0.0f));
  return true;
}
} // namespace

View A2uiRenderer::RenderFlexContainer(const ComponentModel& comp,
                                       const SurfaceComponentsModel& components,
                                       DataContext& ctx,
                                       FlexDirection direction)
{
  FlexLayout flex = FlexLayout::New();
  flex.SetDirection(direction);
  flex.SetRequestedHeight(WRAP_CONTENT);

  float selfWeight = comp.rawNode ? GetNodeFloat(*comp.rawNode, "weight", 0.0f) : 0.0f;
  if(selfWeight <= 0.0f)
  {
    flex.SetRequestedWidth(MATCH_PARENT);
  }

  if(comp.rawNode)
  {
    const char* justify = GetNodeString(*comp.rawNode, "justify", "start");
    if(strcmp(justify, "center") == 0)
      flex.SetJustifyContent(FlexJustify::CENTER);
    else if(strcmp(justify, "end") == 0)
      flex.SetJustifyContent(FlexJustify::FLEX_END);
    else if(strcmp(justify, "spaceBetween") == 0)
      flex.SetJustifyContent(FlexJustify::SPACE_BETWEEN);
    else if(strcmp(justify, "spaceAround") == 0)
      flex.SetJustifyContent(FlexJustify::SPACE_AROUND);
    else if(strcmp(justify, "spaceEvenly") == 0)
      flex.SetJustifyContent(FlexJustify::SPACE_EVENLY);

    const char* align = GetNodeString(*comp.rawNode, "align", "stretch");
    if(strcmp(align, "center") == 0)
      flex.SetAlignItems(FlexAlign::CENTER);
    else if(strcmp(align, "end") == 0)
      flex.SetAlignItems(FlexAlign::FLEX_END);
    else if(strcmp(align, "start") == 0)
      flex.SetAlignItems(FlexAlign::FLEX_START);
    else
      flex.SetAlignItems(FlexAlign::STRETCH);

    const char* bg = GetNodeString(*comp.rawNode, "background", "");
    if(bg[0] != '\0')
    {
      flex.SetBackgroundColor(ParseHexColor(bg));
    }

    float padding = GetNodeFloat(*comp.rawNode, "padding", 0.0f);
    if(padding > 0.0f)
    {
      uint16_t p = static_cast<uint16_t>(Metrics::Dp(padding));
      flex.SetPadding(Extents(p, p, p, p));
    }
  }

  bool isRow = (direction == FlexDirection::ROW || direction == FlexDirection::ROW_REVERSE);
  bool isColumnCentered = !isRow && comp.rawNode &&
    (strcmp(GetNodeString(*comp.rawNode, "align", "start"), "center") == 0);
  // align:end column — its leaf text must RIGHT-align (the flight times "Arrives"/"11:30 PM"
  // column). AlignItems:FLEX_END alone doesn't move a MATCH_PARENT-width Label, so its glyphs
  // stayed left and the column read ~11% short of the card's right edge.
  bool isColumnEnd = !isRow && comp.rawNode &&
    (strcmp(GetNodeString(*comp.rawNode, "align", "start"), "end") == 0);
  // A Column with no explicit align is "centred-style" if it visibly centres its other content
  // (a centered header sub-column, a justify:center row) — an auth/dialog card. There a Button
  // should centre too (card 09 "Sign in"), whereas a left-aligned content column centres nothing
  // and its Button sits at the left (card 29 "Watch Trailer", card 32 "Submit"). This separates
  // the two cases without per-card rules — both columns are otherwise structurally identical.
  bool columnCentredStyle = isColumnCentered;
  if(!isRow && !columnCentredStyle)
  {
    for(const auto& cid : comp.childIds)
    {
      const ComponentModel* cc = components.GetComponent(cid);
      if(!cc || !cc->rawNode) continue;
      // A nested Column that centres its OWN children (align:center → horizontal) or a Row that
      // centres horizontally (justify:center) is a real "centred card" signal. A Row's align:center
      // is just VERTICAL cross-axis centering of an icon+text line (event-detail, rating rows) —
      // neutral, NOT a signal — so check justify for Rows and align for Columns, never both.
      bool sibRow = (cc->type == "Row");
      bool centredSib = sibRow
        ? (strcmp(GetNodeString(*cc->rawNode, "justify", ""), "center") == 0)
        : (strcmp(GetNodeString(*cc->rawNode, "align", ""), "center") == 0);
      if(centredSib) { columnCentredStyle = true; break; }
    }
  }

  // Default gap = the web composer's flex gaps (Row g-4 = 16, Column g-2 = 8); a
  // message-declared `gap` (logical) wins and is dp-scaled to match.
  float defaultGap = isRow ? Metrics::RowGap() : Metrics::ColumnGap();
  float gap = (comp.rawNode && comp.rawNode->Find("gap"))
                ? Metrics::Dp(GetNodeFloat(*comp.rawNode, "gap", 0.0f))
                : defaultGap;

  // --- Text wrap-width budget ---------------------------------------------------------------
  // The width (device px) a descendant Text may use for its line-wrap estimate. A Column hands
  // its full width down; a Row splits it, and a column next to a leading image/checkbox/icon
  // gets only the leftover — so its text wraps EARLIER and must reserve more lines (or it clips,
  // the bug seen on podcast/task). 0 on entry → fall back to the full card content width.
  float savedBudget  = mTextWidthBudget;
  float parentBudget = (mTextWidthBudget > 0.0f) ? mTextWidthBudget : Metrics::CardContentWidth();
  {
    float ownPad = comp.rawNode ? GetNodeFloat(*comp.rawNode, "padding", 0.0f) : 0.0f;
    if(ownPad > 0.0f) parentBudget -= 2.0f * Metrics::Dp(ownPad);
  }
  if(parentBudget < Metrics::Dp(40)) parentBudget = Metrics::CardContentWidth();

  // On-screen width (device px) of a non-container child, for dividing a Row's space.
  auto estChildWidth = [this, &components, parentBudget](const ComponentModel* cc) -> float {
    if(!cc) return Metrics::Dp(40);
    const std::string& t = cc->type;
    if(t == "Image")
    {
      if(cc->rawNode)
      {
        float jw = GetNodeFloat(*cc->rawNode, "width", -1.0f);
        if(jw > 0.0f) return Metrics::Dp(jw);
        const char* v = GetNodeString(*cc->rawNode, "variant", "");
        if(strcmp(v, "avatar") == 0) return Metrics::AvatarSize();
        if(strcmp(v, "icon") == 0)   return Metrics::IconSize();
      }
      return Metrics::RowThumbnail();          // default image in a row renders as a thumbnail
    }
    if(t == "Icon")     return Metrics::Dp(28);
    if(t == "CheckBox") return Metrics::Dp(44);
    if(t == "Button")   return Metrics::ButtonMinWidth();
    if(t == "Text" && cc->rawNode)
    {
      const TreeNode* tn = cc->rawNode->Find("text");
      float fs  = VariantToFontSize(GetNodeString(*cc->rawNode, "variant", "body"));
      float len = (tn && tn->GetType() == TreeNode::STRING)
                    ? static_cast<float>(Utf8Len(tn->GetString(), strlen(tn->GetString()))) : 8.0f;
      float w   = len * fs * 0.58f + fs;
      return (w < parentBudget) ? w : parentBudget;
    }
    return Metrics::Dp(40);
  };

  // For a Row, pre-divide the leftover among the container kids AND the LEAF TEXT kids. A leaf
  // Text in a Row (an "icon + label" line, a "label  value" line) must wrap at the space LEFT
  // after the fixed siblings — NOT at its own estChildWidth (which can't see a data-bound
  // value's length, defaults to 8 chars, and would make text.cpp count 4 phantom lines and
  // stack the row vertically — the event-detail icon-drops-below-text bug).
  float rowColBudget   = parentBudget;   // width handed to a container child
  float rowTextBudget  = parentBudget;   // width handed to a leaf Text child
  // A mixed Row (a growing container sharing the row with FIXED leaves — a checkbox, a leading
  // image, a trailing priority Icon, a "8:09"/"Backend" pinned caption) must give the container
  // a CONCRETE leftover width, not MATCH_PARENT/basis-0. A MATCH_PARENT container grabs the whole
  // row and pushes the trailing fixed leaf off the card edge so it vanishes (the task-card "!" +
  // "Backend", the track-list durations). These hold that concrete width + whether to apply it.
  float rowContainerConcreteWidth      = parentBudget;
  bool  rowContainerNeedsConcreteWidth = false;
  if(isRow && !comp.childIds.empty())
  {
    float nonTextFixed = 0.0f; int containerCount = 0, textCount = 0;
    for(const auto& cid : comp.childIds)
    {
      const ComponentModel* cc = components.GetComponent(cid);
      const std::string t = cc ? cc->type : std::string();
      if(t == "Column" || t == "Row" || t == "Card" || t == "List") ++containerCount;
      else if(t == "Text")                                          ++textCount;
      else nonTextFixed += estChildWidth(cc);   // icon / checkbox / image / fixed leaf
    }
    float gapsTotal = gap * (comp.childIds.size() > 1 ? (comp.childIds.size() - 1) : 0);
    float leftover  = parentBudget - nonTextFixed - gapsTotal;
    int   sharers   = containerCount + textCount;
    if(sharers > 0)
    {
      float share = leftover / sharers;
      if(share < Metrics::Dp(40)) share = Metrics::Dp(40);
      if(containerCount > 0) rowColBudget  = share;
      if(textCount > 0)      rowTextBudget = share;
    }
    // When a growing container shares the row with FIXED leaves (icon/checkbox/image already in
    // nonTextFixed) OR small pinned leaf texts, hand the container the CONCRETE remaining width so
    // those trailing leaves keep their slot. Reserve the leaf texts at their natural width, then
    // split the rest among the container kids. (estChildWidth caps a text at parentBudget, so a
    // long leaf can't over-reserve.)
    if(containerCount > 0 && (nonTextFixed > 0.0f || textCount > 0))
    {
      float textReserved = 0.0f;
      for(const auto& cid : comp.childIds)
      {
        const ComponentModel* cc = components.GetComponent(cid);
        if(cc && cc->type == "Text") textReserved += estChildWidth(cc);
      }
      rowContainerConcreteWidth = (leftover - textReserved) / static_cast<float>(containerCount);
      if(rowContainerConcreteWidth < Metrics::Dp(80)) rowContainerConcreteWidth = Metrics::Dp(80);
      rowContainerNeedsConcreteWidth = true;
      rowColBudget = rowContainerConcreteWidth;   // inner-text wrap budget tracks the real width
    }
    // A row of inline texts (no flex-grow container) whose natural widths together exceed the
    // row WRAPS to the next line instead of overflowing the card — the invitation preview's
    // "Celebrating" + "Alex Johnson" (h3) was spilling past the card edge. Each text then keeps
    // its full width on its own line. (rowTextBudget gives each its full natural size below.)
    if(containerCount == 0 && textCount >= 2)
    {
      float totalNatural = nonTextFixed + gapsTotal;
      for(const auto& cid : comp.childIds)
      {
        const ComponentModel* cc = components.GetComponent(cid);
        if(cc && cc->type == "Text") totalNatural += estChildWidth(cc);
      }
      if(totalNatural > parentBudget)
      {
        flex.SetWrap(FlexWrap::WRAP);
        rowTextBudget = parentBudget;   // a wrapped text gets the full row width on its line
      }
    }
  }

  // Data-driven child list (children: {path, componentId}) — instantiate the template per
  // array element. Must run before the static childIds loop, which would render nothing
  // (the parser leaves childIds empty for the OBJECT form). Template runs are list rows, so
  // they pack with the tighter list gap (unless the message set an explicit gap).
  float tmplGap = (comp.rawNode && comp.rawNode->Find("gap")) ? gap : Metrics::ListItemGap();
  mTextWidthBudget = isRow ? rowColBudget : parentBudget;
  if(RenderTemplateChildren(comp, components, ctx, flex, isRow, tmplGap))
  {
    mTextWidthBudget = savedBudget;
    return flex;
  }

  // A justify of spaceBetween/Around/Evenly distributes children itself — in that case a
  // child must NOT flex-grow, or the grown child eats the space the distribution would have
  // given the trailing item (e.g. a line-item price "$6.45" clipped to "$6").
  const char* rowJustify = comp.rawNode ? GetNodeString(*comp.rawNode, "justify", "start") : "start";
  bool distributed = isRow && (strcmp(rowJustify, "spaceBetween") == 0 ||
                               strcmp(rowJustify, "spaceAround") == 0 ||
                               strcmp(rowJustify, "spaceEvenly") == 0);

  // A cross-axis `align` DECLARED on the Row is the author's instruction for every child, images
  // included. Pinning an image's width below must not also pin its cross alignment, or a Row that
  // asked for align:start renders its thumbnail/avatar vertically CENTRED instead (the podcast
  // artwork sank ~70px below the show name; the chat avatars ~25px below their username). Rows
  // that declare nothing keep the implicit CENTER, which is what an icon/thumb + text line wants.
  const char* rowAlign = comp.rawNode ? GetNodeString(*comp.rawNode, "align", "") : "";
  bool rowAlignDeclared = (strcmp(rowAlign, "start") == 0 || strcmp(rowAlign, "end") == 0 ||
                           strcmp(rowAlign, "center") == 0);

  // A Row whose children are ALL containers (e.g. Departs/Status/Arrives columns) wants
  // them split evenly. A Row with MIXED children (text + image + one info column) wants
  // the container to fill the *remaining* space. These need different flex seeding, so
  // detect the all-container case up front.
  bool allRowChildrenContainers = isRow && !comp.childIds.empty();
  if(allRowChildrenContainers)
  {
    for(const auto& cid : comp.childIds)
    {
      const ComponentModel* cc = components.GetComponent(cid);
      const std::string t = cc ? cc->type : std::string();
      if(t != "Column" && t != "Row" && t != "Card" && t != "List") { allRowChildrenContainers = false; break; }
    }
  }

  int index = 0;
  for(const auto& childId : comp.childIds)
  {
    const ComponentModel* childComp = components.GetComponent(childId);
    // A default-sized Image placed directly in a Row is a list thumbnail — hint
    // RenderImage to build it small so it doesn't overflow the row. (Shrinking the
    // wrapper after the fact doesn't work: the data-binding container rebuilds the
    // full-size inner image when its url arrives.)
    if(isRow && childComp && childComp->type == "Image" && childComp->rawNode &&
       childComp->rawNode->Find("width") == nullptr &&
       childComp->rawNode->Find("height") == nullptr &&
       GetNodeString(*childComp->rawNode, "variant", "")[0] == '\0')
    {
      mImageThumbnailHint = true;
    }
    // An avatar that sits inside a Row (chat bubble, list item) is a small inline avatar, not
    // a centred hero — flag it so RenderImage uses the small box instead of the 100dp hero.
    if(isRow && childComp && childComp->type == "Image" && childComp->rawNode &&
       strcmp(GetNodeString(*childComp->rawNode, "variant", ""), "avatar") == 0)
    {
      mAvatarSmallHint = true;
    }
    // Give this child the width it will actually occupy, so a Text inside it wraps at the same
    // place as the web: a Row's container child gets the divided column budget, a leaf gets its
    // own estimate, and a Column's child gets the full parent width.
    if(isRow)
    {
      const std::string ct = childComp ? childComp->type : std::string();
      bool childIsContainer = (ct == "Column" || ct == "Row" || ct == "Card" || ct == "List");
      if(childIsContainer)   mTextWidthBudget = rowColBudget;
      // A leaf Text in a DISTRIBUTED row (spaceBetween/around/evenly) takes its NATURAL width and
      // does NOT wrap — give it the FULL row width as its wrap budget so a short title like "The
      // Italian Kitchen" reserves 1 line. (estChildWidth can't see a DATA-BOUND value's real
      // length — it defaults to 8 chars — so it under-budgets and forces a phantom 2nd line that
      // inflates the row and drops the trailing "$$$" half a line via align:center.)
      else if(ct == "Text")  mTextWidthBudget = distributed ? parentBudget : rowTextBudget;
      else                   mTextWidthBudget = estChildWidth(childComp);
    }
    else
    {
      mTextWidthBudget = parentBudget;
    }
    View child = RenderComponent(childId, components, ctx);
    mImageThumbnailHint = false;
    mAvatarSmallHint = false;

    // (Tried halving the gap around Dividers to slim carded forms — but the web's divider spacing
    // varies per card, so it over-shortened coffee/purchase and DROPPED the mean. Reverted: the
    // ±1-2px per-card spacing drift is a documented limit, not cleanly fixable with one rule.)
    ApplyItemGap(child, isRow, gap, index == 0);

    // Column align=center: centre Label text, and STRETCH full-width children so they
    // don't collapse to zero width — DALi FlexLayout collapses a MATCH_PARENT child
    // under align-items:center (which silently blanked e.g. a Slider in a centred
    // Column). Narrow children (Button/Image) keep their own AlignSelf:CENTER.
    if(isColumnCentered)
    {
      Label labelChild = Label::DownCast(child);
      if(labelChild)
      {
        // The web's align-items:center centres each child BOX, but the text inside keeps its
        // default text-align:left — so a SHORT (one-line) label looks centred while a WRAPPED
        // paragraph stays left (the user-profile bio). Match that: centre the glyphs only when
        // the label fits on one line; let a multi-line label keep its default (left) alignment.
        float lfs = (childComp && childComp->rawNode)
                      ? VariantToFontSize(GetNodeString(*childComp->rawNode, "variant", "body"))
                      : Metrics::FontBody();
        float budget   = (mTextWidthBudget > 0.0f) ? mTextWidthBudget : Metrics::CardContentWidth();
        // This is a decision about the label's TEXT, so it is frozen for a bound label the same
        // way a width is: an empty string always "fits on one line", so a streamed paragraph
        // stays centred forever. Re-decide when the value lands. Falling back to the alignment
        // the label already had (rather than to START) keeps a message-declared `align` intact.
        Text::Alignment declared = labelChild.GetHorizontalTextAlignment();
        auto centreIfItFitsOneLine = [labelChild, lfs, budget, declared]() mutable {
          Dali::String t = labelChild.GetText();
          float naturalW = TextNaturalWidth(t.CStr(), t.Size(), lfs);  // glyph-weighted, not bytes
          labelChild.SetHorizontalTextAlignment(naturalW <= budget ? Text::Alignment::CENTER
                                                                   : declared);
        };
        centreIfItFitsOneLine();
        if(childComp && childComp->rawNode)
        {
          WatchBinding(childComp->rawNode->Find("text"), ctx,
                       [centreIfItFitsOneLine](const std::string&) mutable { centreIfItFitsOneLine(); });
        }
      }
      std::string ct = childComp ? childComp->type : std::string();
      bool fullWidth = labelChild || ct == "TextField" || ct == "Slider" ||
                       ct == "ProgressBar" || ct == "List" || ct == "Divider" ||
                       ct == "Video" || ct == "AudioPlayer";
      if(fullWidth)
      {
        child.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::STRETCH));
      }
    }
    // align:end column → right-align its leaf text (the MATCH_PARENT Label fills the column, so
    // AlignItems:FLEX_END can't move it; the glyphs need text-align:end). Fixes the flight
    // "Arrives"/"11:30 PM" sitting ~11% short of the card's right edge.
    else if(isColumnEnd)
    {
      Label labelChild = Label::DownCast(child);
      if(labelChild) labelChild.SetHorizontalTextAlignment(Text::Alignment::END);
    }
    // A Button in a LEFT-aligned content Column sits at the column's start (left) — button.cpp
    // pins AlignSelf:CENTER, but a left content column (movie "Watch Trailer", form "Submit")
    // keeps the button left like web. In a centred-style column (auth/dialog card, e.g. 09
    // "Sign in") we leave the button's CENTER so it stays centred with the rest of the content.
    else if(!isRow && childComp && childComp->type == "Button" && !columnCentredStyle)
    {
      child.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::FLEX_START));
    }

    if(childComp && childComp->rawNode)
    {
      float weight = GetNodeFloat(*childComp->rawNode, "weight", 0.0f);
      if(weight > 0.0f)
      {
        child.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(weight).SetFlexBasis(0.0f));
        child.SetRequestedWidth(0.0f);
        // NOTE: the web keeps each weighted column ≥ its content (flex min-width:auto) so the grid
        // OVERFLOWS the card and only the last column clips at the edge. A DALi MinimumWidth on the
        // value cells reproduced "Price in full" but, with basis-0 grow, the asset-info column
        // (weight 2, a nested Row) then failed to claim its remainder and its name clipped to "..".
        // So we keep the fit-to-width weighting (long values ellipsize) — see report card 33 note.
      }
      else if(isRow)
      {
        const std::string& childType = childComp->type;
        // Full-width controls (inputs/sliders) behave like containers in a Row: they should
        // flex-grow to fill, not collapse to content width (a Row's "Due" DateTimeInput
        // otherwise vanishes). CheckBox stays a non-container so it keeps its small size.
        bool isContainer = (childType == "Column" || childType == "Row" || childType == "Card" ||
                            childType == "List" || childType == "DateTimeInput" ||
                            childType == "TextField" || childType == "Slider" ||
                            childType == "ChoicePicker");
        if(isContainer)
        {
          if(distributed)
          {
            // justify spaceBetween/Around/Evenly: grow the container from a 0 basis (and a 0
            // requested width, or a MATCH_PARENT child would fill the whole row and hide its
            // siblings — e.g. flight Departs/Status/Arrives showing only Departs). This keeps
            // every column visible AND leaves room for a pinned trailing item (price/value).
            child.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(0.0f));
            child.SetRequestedWidth(0.0f);
          }
          else if(allRowChildrenContainers)
          {
            // All siblings are containers → split the row evenly. basis 0 + a 0 requested
            // width seed equal grow distribution (e.g. flightStatus Departs/Status/Arrives).
            child.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(0.0f));
            child.SetRequestedWidth(0.0f);
          }
          else if(rowContainerNeedsConcreteWidth)
          {
            // Mixed row (a fixed checkbox/image/trailing icon or a pinned "8:09"/"Backend"
            // caption + this info column). Give the column the CONCRETE leftover width (row minus
            // the fixed/pinned leaves) so (a) its inner text still measures against a real width —
            // RequestedWidth 0 blanked it, and (b) the trailing fixed leaf keeps its slot —
            // MATCH_PARENT grabbed the whole row and pushed the "!"/duration/"Backend" off the
            // card. Grow/shrink lets the column absorb any rounding slack without overrunning.
            child.SetRequestedWidth(rowContainerConcreteWidth);
            child.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f).SetFlexShrink(1.0f));
          }
          else
          {
            // Lone container in the row (no fixed/pinned sibling to protect) → fill from a 0
            // basis. Basis 0 (not RequestedWidth 0) preserves the column's MATCH_PARENT inner
            // text — RequestedWidth 0 was what blanked it before.
            child.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(0.0f));
          }
        }
        else
        {
          // Text/other in a Row: use natural width, allow shrink.
          bool pinned = false;
          Label labelChild = Label::DownCast(child);
          if(labelChild)
          {
            float fs = (childComp && childComp->rawNode)
              ? VariantToFontSize(GetNodeString(*childComp->rawNode, "variant", "body")) : Metrics::FontBody();
            pinned = PinShortRowLabel(labelChild, fs);
            // The reservation above measured the label AS IT IS RIGHT NOW, and a bound label is
            // empty at this point — its value arrives in a later updateDataModel. Re-measure when
            // it does, or the streamed value lands in the slot cut for the empty string and a
            // flex-grow sibling squeezes it away. Registered after the child render, so the
            // label's own watch has already written the new text by the time we read it back.
            // WatchBinding is a no-op for a literal label, which cannot change.
            if(childComp && childComp->rawNode)
            {
              WatchBinding(childComp->rawNode->Find("text"), ctx,
                           [labelChild, fs](const std::string&) mutable {
                             PinShortRowLabel(labelChild, fs);
                           });
            }
          }
          else if(childType == "Image")
          {
            // A responsive image is MATCH_PARENT (fill container up to its declared max-width). On a
            // Row's MAIN axis the layout engine forces MATCH_PARENT children to the full width,
            // squeezing siblings — so pin the image to its declared width (stashed as MaximumWidth).
            // Default-sized row images were already built thumbnail-small by the hint above.
            // RequestedWidth alone is NOT honoured for a nested-flex child (a DATA-BOUND image returns
            // a FlexLayout container): its main-axis slot stays responsive and shrinks below the drawn
            // image at narrow (phone) widths, so a sibling text Column overlaps it (podcast/purchase
            // Row[image,text]). A definite flex-basis + grow/shrink 0 pins the slot for both forms.
            float declared = child.GetMaximumWidth();
            if(declared > 0.0f && declared < 100000.0f)
            {
              // AUTO defers to the Row's alignItems, so a declared align:start/end/center wins;
              // only an undeclared Row falls back to the implicit CENTER (see rowAlignDeclared).
              child.SetRequestedWidth(declared);
              child.SetLayoutParams(FlexLayoutParams::New()
                                      .SetAlignSelf(rowAlignDeclared ? FlexAlign::AUTO : FlexAlign::CENTER)
                                      .SetFlexGrow(0.0f).SetFlexShrink(0.0f).SetFlexBasis(declared));
              pinned = true;
            }
          }
          else if(childType == "Icon")
          {
            // A small leading/trailing Icon (a row bullet, a priority "!" , a trend arrow) has a
            // fixed size and must NOT shrink — otherwise a flex-grown sibling squeezes it to zero
            // and it vanishes (the task-card "!" and "Backend" tag disappearing).
            child.SetLayoutParams(FlexLayoutParams::New().SetFlexShrink(0.0f));
            pinned = true;
          }
          else if(child.GetRequestedWidth() == MATCH_PARENT)
          {
            // A non-container that declares full width (e.g. a CheckBox's row) would grab the
            // whole Row on the main axis and push its siblings off — size it to content.
            child.SetRequestedWidth(WRAP_CONTENT);
          }
          if(!pinned)
          {
            child.SetLayoutParams(FlexLayoutParams::New().SetFlexShrink(1.0f));
          }
        }
      }
    }

    flex.Add(child);
    index++;
  }

  mTextWidthBudget = savedBudget;
  return flex;
}
} // namespace A2ui
