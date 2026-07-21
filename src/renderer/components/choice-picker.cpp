#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderChoicePicker(const ComponentModel& comp, DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  // displayStyle:"chips" → a horizontal row of selectable pills (web segmented look);
  // otherwise a vertical radio list. Both single-select (mutuallyExclusive).
  bool chips = (strcmp(GetNodeString(*comp.rawNode, "displayStyle", ""), "chips") == 0);

  FlexLayout container = FlexLayout::New();
  container.SetDirection(chips ? FlexDirection::ROW : FlexDirection::COLUMN);
  container.SetRequestedWidth(MATCH_PARENT);
  container.SetRequestedHeight(WRAP_CONTENT);
  container.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(4)), static_cast<uint16_t>(Metrics::Dp(4))));
  if(chips)
  {
    container.SetAlignItems(FlexAlign::CENTER);
    container.SetWrap(FlexWrap::WRAP);  // chips flow to new rows in a narrow column (e.g. the
                                        // invitation Location picker) instead of clipping to "Gr…"
  }

  // Label (optional). A vertical list puts it above its rows directly. A chips picker is a ROW
  // (the pills flow horizontally), so its label can't go INSIDE the row — it must sit ABOVE in a
  // wrapping Column built at the return. Earlier the chips branch dropped the label entirely, so
  // the invitation builder's "Location" field had NO label while every sibling field showed one.
  const char* labelText = GetNodeString(*comp.rawNode, "label", "");
  Label headerLabel;
  if(labelText[0] != '\0')
  {
    headerLabel = Label::New(labelText);
    headerLabel.SetFontSize(Metrics::FontLabel());
    headerLabel.SetTextColor(A2uiTheme::Color("OnSurfaceContainerLow"));
    headerLabel.SetRequestedWidth(MATCH_PARENT);
    headerLabel.SetRequestedHeight(WRAP_CONTENT);
    headerLabel.SetMargin(Extents(0, 0, 0, static_cast<uint16_t>(Metrics::Dp(4))));
    if(!chips) container.Add(headerLabel);
  }

  const TreeNode* valueNode = comp.rawNode->Find("value");
  std::string boundPath = valueNode ? GetBoundPath(valueNode, ctx) : "";

  // The selected value may be a plain string OR an array (mutuallyExclusive pickers store
  // ["annual"]). isSelected resolves the live bound node and tests membership either way, so
  // it works for both the initial paint and the reactive watch.
  DataContext selCtx = ctx;
  auto isSelected = [selCtx, boundPath](const std::string& v) mutable -> bool {
    if(boundPath.empty()) return false;
    const TreeNode* n = selCtx.GetDataModel().ResolvePath(boundPath);
    if(n && n->GetType() == TreeNode::ARRAY)
    {
      for(auto it = n->CBegin(); it != n->CEnd(); ++it)
        if((*it).second.GetType() == TreeNode::STRING && (*it).second.GetString() == v) return true;
      return false;
    }
    return selCtx.GetDataModel().GetString(boundPath) == v;
  };

  const TreeNode* optionsNode = comp.rawNode->Find("options");
  if(!optionsNode || optionsNode->GetType() != TreeNode::ARRAY)
  {
    return container;
  }

  // (view, value, label) per option, for reactive re-styling on selection change.
  struct Opt { View row; Label lbl; View icon; std::string value; };
  auto opts = std::make_shared<std::vector<Opt>>();

  int index = 0;
  for(auto it = optionsNode->CBegin(); it != optionsNode->CEnd(); ++it)
  {
    const TreeNode& optNode = (*it).second;
    const TreeNode* optLabelNode = optNode.Find("label");
    const TreeNode* optValueNode = optNode.Find("value");
    std::string optLabel = (optLabelNode && optLabelNode->GetType() == TreeNode::STRING)
                           ? optLabelNode->GetString() : "";
    std::string optValue = (optValueNode && optValueNode->GetType() == TreeNode::STRING)
                           ? optValueNode->GetString() : "";
    bool selected = isSelected(optValue);

    FlexLayout optRow = FlexLayout::New();
    optRow.SetDirection(FlexDirection::ROW);
    optRow.SetAlignItems(FlexAlign::CENTER);
    optRow.SetRequestedHeight(WRAP_CONTENT);

    Label optLbl = Label::New(optLabel.c_str());
    optLbl.SetRequestedWidth(WRAP_CONTENT);
    // Reserve the line height explicitly — a WRAP_CONTENT label inside a sized pill/row can
    // measure height 0 (its width resolves only after layout) and vanish.
    optLbl.SetRequestedHeight(Metrics::LineHeight(Metrics::FontChoice()));
    View radioIcon;  // only the list style has a radio dot

    if(chips)
    {
      // Pill chip: selected = dark fill + white label, unselected = card-coloured + dark
      // label. Sized to content with comfortable padding; pill radius. The label needs an
      // explicit width — a WRAP_CONTENT label inside a justify-centre pill measures width 0
      // (its size resolves only after layout) and the pill collapses to just its padding.
      optRow.SetJustifyContent(FlexJustify::CENTER);
      optRow.SetRequestedWidth(WRAP_CONTENT);
      float lblW = static_cast<float>(optLabel.size()) * Metrics::FontChoice() * 0.62f + Metrics::Dp(4);
      optLbl.SetRequestedWidth(lblW);
      uint16_t px = static_cast<uint16_t>(Metrics::Dp(14));
      uint16_t py = static_cast<uint16_t>(Metrics::Dp(7));
      optRow.SetPadding(Extents(px, px, py, py));
      optRow.SetCornerRadius(Metrics::Dp(16));
      // selected = dark fill + white label; unselected = card-coloured (blends into the
      // white card, like the web's plain-text chip) + dark label. (DALi treats 0x00000000
      // as opaque black, so the unselected fill must be the card colour, not "transparent".)
      optRow.SetBackgroundColor(selected ? COLOR_TEXT_DEFAULT : COLOR_CARD_BG);
      // Unselected chips show the web's hairline OUTLINE (otherwise a white-on-white pill that
      // reads as plain text); the selected chip's dark fill is its own emphasis, no border.
      if(selected)
      {
        optRow.SetBorderlineWidth(0.0f);
      }
      else
      {
        optRow.SetBorderlineWidth(Metrics::BorderInput());
        optRow.SetBorderlineColor(COLOR_BTN_BORDER);
      }
      // Inter-chip gap via RIGHT margin (not left): a left margin on chip #2,#3 INDENTED them
      // when the picker wrapped to a vertical stack (narrow Location column), breaking the left
      // edge. Right margin spaces chips on a row yet keeps every wrapped chip flush-left. Bottom
      // margin separates wrapped rows. Extents = (left, right, top, bottom).
      optRow.SetMargin(Extents(0, static_cast<uint16_t>(Metrics::Dp(8)),
                               0, static_cast<uint16_t>(Metrics::Dp(6))));
      optLbl.SetFontSize(Metrics::FontChoice());
      optLbl.SetTextColor(selected ? COLOR_CARD_BG : COLOR_TEXT_DEFAULT);
      optRow.Add(optLbl);
    }
    else
    {
      optRow.SetRequestedWidth(MATCH_PARENT);
      optRow.SetPadding(Extents(static_cast<uint16_t>(Metrics::Dp(8)), static_cast<uint16_t>(Metrics::Dp(8)),
                                static_cast<uint16_t>(Metrics::Dp(6)), static_cast<uint16_t>(Metrics::Dp(6))));
      radioIcon = View::New();
      radioIcon.SetRequestedWidth(Metrics::Dp(20));
      radioIcon.SetRequestedHeight(Metrics::Dp(20));
      radioIcon.SetCornerRadius(Metrics::RadiusRadio());
      radioIcon.SetBorderlineWidth(Metrics::BorderInput());
      radioIcon.SetBorderlineColor(A2uiTheme::Color("Outline"));
      radioIcon.SetBackgroundColor(selected ? COLOR_CHECK_ON : COLOR_CHECK_OFF);
      optRow.Add(radioIcon);
      optLbl.SetFontSize(Metrics::FontChoice());
      optLbl.SetTextColor(COLOR_TEXT_DEFAULT);
      optLbl.SetMargin(Extents(static_cast<uint16_t>(Metrics::Dp(8)), 0, 0, 0));
      optRow.Add(optLbl);
    }

    if(!boundPath.empty())
    {
      // mutuallyExclusive pickers store the selection as a one-element array; preserve that
      // shape so the value round-trips the way the server sent it.
      bool arrayShape = chips;
      // dali-ui 2.5.26: AsInteractive() now returns the InteractiveTrait directly
      // (no lambda configurator). Attach the click handler to the returned trait.
      InteractiveTrait trait = optRow.AsInteractive();
      trait.ClickedSignal().Connect(this,
        [optValue, boundPath, ctx, arrayShape](View, const InputEvent&) mutable -> bool {
          if(arrayShape) ctx.GetDataModel().SetData(boundPath, "[\"" + optValue + "\"]");
          else           ctx.GetDataModel().SetValue(boundPath, optValue);
          return true;
        });
    }

    opts->push_back({optRow, optLbl, radioIcon, optValue});
    container.Add(optRow);
    index++;
  }

  // Watch for value changes → restyle every option's selected state (re-resolves the bound
  // node via isSelected so array-shaped selections restyle correctly too).
  WatchBinding(valueNode, ctx, [opts, chips, isSelected](const std::string&) mutable {
    for(auto& o : *opts)
    {
      bool sel = isSelected(o.value);
      if(chips)
      {
        o.row.SetBackgroundColor(sel ? COLOR_TEXT_DEFAULT : COLOR_CARD_BG);
        o.lbl.SetTextColor(sel ? COLOR_CARD_BG : COLOR_TEXT_DEFAULT);
        o.row.SetBorderlineWidth(sel ? 0.0f : Metrics::BorderInput());
        o.row.SetBorderlineColor(COLOR_BTN_BORDER);
      }
      else if(o.icon)
      {
        o.icon.SetBackgroundColor(sel ? COLOR_CHECK_ON : COLOR_CHECK_OFF);
      }
    }
  });

  // Chips picker WITH a label: stack the label above the wrapping chip row in an outer Column so
  // it lines up with the other form fields' labels (e.g. the invitation builder "Location").
  if(chips && headerLabel)
  {
    FlexLayout outer = FlexLayout::New();
    outer.SetDirection(FlexDirection::COLUMN);
    outer.SetRequestedWidth(MATCH_PARENT);
    outer.SetRequestedHeight(WRAP_CONTENT);
    outer.Add(headerLabel);
    outer.Add(container);
    return outer;
  }

  return container;
}
} // namespace A2ui
