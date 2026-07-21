#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderTabs(const ComponentModel& comp,
                              const SurfaceComponentsModel& components,
                              DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  FlexLayout container = FlexLayout::New();
  container.SetDirection(FlexDirection::COLUMN);
  container.SetRequestedWidth(MATCH_PARENT);
  container.SetRequestedHeight(WRAP_CONTENT);

  const TreeNode* tabsNode = comp.rawNode->Find("tabs");
  if(!tabsNode || tabsNode->GetType() != TreeNode::ARRAY)
  {
    return container;
  }

  // Tab header row (web style: underline tabs, not filled pills) with a full-width hairline
  // under it; the active tab shows a dark underline bar + dark label, the rest are muted.
  FlexLayout tabHeader = FlexLayout::New();
  tabHeader.SetDirection(FlexDirection::ROW);
  tabHeader.SetRequestedWidth(MATCH_PARENT);
  tabHeader.SetRequestedHeight(WRAP_CONTENT);
  // NO borderline here — SetBorderlineWidth draws a full BOX around the tab row (the web shows
  // only a hairline UNDER the tabs). The bottom hairline is added as a separate full-width line
  // below, and the active tab's underline bar sits on top of it.

  // Content area — only the active tab's content is parented here, so hidden tabs take no
  // vertical space (previously every tab's content was added and inflated the card height).
  FlexLayout contentArea = FlexLayout::New();
  contentArea.SetDirection(FlexDirection::COLUMN);
  contentArea.SetRequestedWidth(MATCH_PARENT);
  contentArea.SetRequestedHeight(WRAP_CONTENT);
  contentArea.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(12)), 0));

  std::vector<View>  tabContentViews;
  std::vector<Label> tabLabels;
  std::vector<View>  tabUnderlines;

  int tabIndex = 0;
  for(auto it = tabsNode->CBegin(); it != tabsNode->CEnd(); ++it)
  {
    const TreeNode& tabDef = (*it).second;
    const TreeNode* titleNode = tabDef.Find("title");
    const TreeNode* childNode = tabDef.Find("child");
    std::string title = (titleNode && titleNode->GetType() == TreeNode::STRING) ? titleNode->GetString() : "Tab";
    std::string childId = (childNode && childNode->GetType() == TreeNode::STRING) ? childNode->GetString() : "";
    bool active = (tabIndex == 0);

    // Tab button = a Column [label, underline bar] so the active underline sits on the
    // header's hairline. No background fill (web underline tabs).
    FlexLayout tabBtn = FlexLayout::New();
    tabBtn.SetDirection(FlexDirection::COLUMN);
    tabBtn.SetAlignItems(FlexAlign::CENTER);
    tabBtn.SetRequestedWidth(WRAP_CONTENT);
    tabBtn.SetRequestedHeight(WRAP_CONTENT);
    if(tabIndex > 0) tabBtn.SetMargin(Extents(static_cast<uint16_t>(Metrics::Dp(16)), 0, 0, 0));

    Label tabLabel = Label::New(title.c_str());
    tabLabel.SetFontSize(Metrics::FontButton());
    tabLabel.SetTextColor(active ? COLOR_TEXT_DEFAULT : COLOR_TEXT_MUTED);
    tabLabel.SetFontWeight(active ? Text::FontWeight::SEMI_BOLD : Text::FontWeight::NORMAL);
    tabLabel.SetRequestedWidth(WRAP_CONTENT);
    tabLabel.SetRequestedHeight(WRAP_CONTENT);
    tabLabel.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(10)), static_cast<uint16_t>(Metrics::Dp(8))));
    tabBtn.Add(tabLabel);

    View underline = View::New();
    underline.SetRequestedWidth(WRAP_CONTENT);
    underline.SetRequestedHeight(Metrics::Dp(2));
    underline.SetBackgroundColor(active ? COLOR_TEXT_DEFAULT : UiColor(0.0f, 0.0f, 0.0f, 0.0f));
    underline.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::STRETCH));
    tabBtn.Add(underline);

    tabHeader.Add(tabBtn);
    tabLabels.push_back(tabLabel);
    tabUnderlines.push_back(underline);

    View tabContent = childId.empty() ? View::New() : RenderComponent(childId, components, ctx);
    tabContent.SetRequestedWidth(MATCH_PARENT);
    tabContentViews.push_back(tabContent);
    if(active) contentArea.Add(tabContent);

    // Selecting this tab: highlight its label + underline, mute the others, and swap the
    // content area to this tab's child. One lambda drives both touch (tap) and the remote
    // OK key so the two paths can never diverge.
    {
      int i = tabIndex;
      auto selectTab = [i, contentArea, tabLabels, tabUnderlines, tabContentViews]() mutable {
        for(int j = 0; j < static_cast<int>(tabLabels.size()); ++j)
        {
          bool sel = (j == i);
          tabLabels[j].SetTextColor(sel ? COLOR_TEXT_DEFAULT : COLOR_TEXT_MUTED);
          tabLabels[j].SetFontWeight(sel ? Text::FontWeight::SEMI_BOLD : Text::FontWeight::NORMAL);
          tabUnderlines[j].SetBackgroundColor(sel ? COLOR_TEXT_DEFAULT : UiColor(0.0f, 0.0f, 0.0f, 0.0f));
        }
        while(contentArea.GetChildCount() > 0) contentArea.Remove(contentArea.GetChildAt(0u));
        contentArea.Add(tabContentViews[i]);
      };

      Dali::TapGestureDetector det = Dali::TapGestureDetector::New();
      det.Attach(tabBtn);
      det.DetectedSignal().Connect(this,
        [selectTab](Dali::Actor, const Dali::TapGesture&) mutable { selectTab(); });
      RetainTapDetector(det);

      // TV remote: focus the tab button; OK/Enter selects it.
      EnableKeyActivation(tabBtn, [selectTab]() mutable { selectTab(); });
    }

    tabIndex++;
  }

  container.Add(tabHeader);
  // Full-width bottom hairline under the tab row (the web tab bar's bottom border).
  View headerLine = View::New();
  headerLine.SetRequestedWidth(MATCH_PARENT);
  headerLine.SetRequestedHeight(Metrics::BorderInput());
  headerLine.SetBackgroundColor(A2uiTheme::Color("OutlineLow"));
  container.Add(headerLine);
  container.Add(contentArea);
  return container;
}
} // namespace A2ui
