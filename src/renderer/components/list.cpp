#include "renderer/render-internal.h"

namespace A2ui
{

bool A2uiRenderer::RenderTemplateChildren(const ComponentModel& comp,
                                          const SurfaceComponentsModel& components,
                                          DataContext& ctx, View outContainer,
                                          bool isRow, float gap)
{
  if(!comp.rawNode) return false;
  const TreeNode* childrenNode = comp.rawNode->Find("children");
  if(!childrenNode || childrenNode->GetType() != TreeNode::OBJECT) return false;
  const TreeNode* cidNode  = childrenNode->Find("componentId");
  const TreeNode* pathNode = childrenNode->Find("path");
  if(!(cidNode && cidNode->GetType() == TreeNode::STRING &&
       pathNode && pathNode->GetType() == TreeNode::STRING))
  {
    return false;  // not the {componentId, path} form (e.g. the inline template{} form)
  }

  std::string tmplId          = cidNode->GetString();
  std::string dataBindingPath = ctx.Resolve(pathNode->GetString());
  const TreeNode* arrayNode   = ctx.GetDataModel().ResolvePath(dataBindingPath);
  if(arrayNode && arrayNode->GetType() == TreeNode::ARRAY)
  {
    int itemIndex = 0;
    for(auto it = arrayNode->CBegin(); it != arrayNode->CEnd(); ++it)
    {
      DataContext childCtx =
        ctx.CreateChildContext(dataBindingPath + "/" + std::to_string(itemIndex));
      View itemView = RenderComponent(tmplId, components, childCtx);
      if(itemView)
      {
        // In a Column the template rows fill the full width; in a Row the items share it
        // evenly (a 0 basis + 0 width, or a MATCH_PARENT item fills the row and hides the
        // rest — e.g. a weather forecast showing only the first day).
        if(!isRow)
        {
          itemView.SetLayoutParams(FlexLayoutParams::New().SetAlignSelf(FlexAlign::STRETCH));
        }
        else
        {
          itemView.SetLayoutParams(
            FlexLayoutParams::New().SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(0.0f));
          itemView.SetRequestedWidth(0.0f);
        }
        ApplyItemGap(itemView, isRow, gap, itemIndex == 0);
        outContainer.Add(itemView);
      }
      itemIndex++;
    }
  }
  return true;  // recognized the template form (even if the array was empty)
}

View A2uiRenderer::RenderList(const ComponentModel& comp,
                              const SurfaceComponentsModel& components,
                              DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  // A List is a content-sized layout, NOT a scroll viewport: the gallery cards never scroll
  // inside a List, and the web renders the rows inline. Wrapping it in a ScrollView made the
  // ScrollView's WRAP_CONTENT height collapse to ~0 when the List sat inside a flex-weighted
  // column of a Row (the incremental-dashboard logs panel vanished) — the inner FlexLayout's
  // content height didn't propagate up through the ScrollView. So we return the FlexLayout
  // directly; its WRAP_CONTENT height resolves correctly in every flex context.

  // Direction: "vertical" (default) → COLUMN, "horizontal" → ROW
  std::string direction = GetNodeString(*comp.rawNode, "direction", "vertical");
  bool horizontal = (direction == "horizontal");

  // Web CSS-flexbox model: a horizontal-list item must not keep MATCH_PARENT
  // width (the first item would fill the row and push the rest off-screen).
  // Make each item a flex item (CSS `flex: 1 1 0`) so items share the row and
  // shrink to fit — all lay out side-by-side and fill the width. The vertical
  // list is unchanged (there MATCH_PARENT width is the cross axis = stretch).
  auto sizeItemMainAxis = [horizontal](View item) {
    if(horizontal && item && item.GetRequestedWidth() == MATCH_PARENT)
    {
      item.SetLayoutParams(FlexLayoutParams::New().SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(0.0f));
      item.SetRequestedWidth(0.0f);
    }
  };

  FlexLayout listContainer = FlexLayout::New();
  listContainer.SetDirection(horizontal ? FlexDirection::ROW : FlexDirection::COLUMN);
  listContainer.SetRequestedWidth(MATCH_PARENT);
  listContainer.SetRequestedHeight(WRAP_CONTENT);

  // A message-declared `gap` (logical) wins and is dp-scaled; otherwise list rows use the
  // tighter list-item gap (a run of similar rows packs closer than distinct sections).
  float gap = comp.rawNode->Find("gap")
                ? Metrics::Dp(GetNodeFloat(*comp.rawNode, "gap", 0.0f))
                : Metrics::ListItemGap();

  // Check for template children (data-driven list)
  const TreeNode* childrenNode = comp.rawNode->Find("children");

  if(childrenNode && childrenNode->GetType() == TreeNode::OBJECT)
  {
    // Official A2UI v0.9 format: children = { componentId, path }
    // componentId references another component in the surface-level components
    // map; path resolves to an ARRAY in the DataModel whose each element
    // becomes a child scope.
    const TreeNode* cidNode  = childrenNode->Find("componentId");
    const TreeNode* pathNode = childrenNode->Find("path");
    if(cidNode  && cidNode->GetType()  == TreeNode::STRING
       && pathNode && pathNode->GetType() == TreeNode::STRING)
    {
      // Official v0.9 payload recognized. Always return a list container from
      // this branch — even if the data path resolves to empty/non-ARRAY — so
      // we don't accidentally fall through to the template or childIds paths.
      std::string tmplId = cidNode->GetString();
      std::string dataBindingPath = ctx.Resolve(pathNode->GetString());
      const Dali::Ui::Integration::TreeNode* arrayNode =
        ctx.GetDataModel().ResolvePath(dataBindingPath);

      if(arrayNode && arrayNode->GetType() == TreeNode::ARRAY)
      {
        int itemIndex = 0;
        for(auto arrIt = arrayNode->CBegin(); arrIt != arrayNode->CEnd(); ++arrIt)
        {
          DataContext childCtx =
            ctx.CreateChildContext(dataBindingPath + "/" + std::to_string(itemIndex));

          View itemView = RenderComponent(tmplId, components, childCtx);
          sizeItemMainAxis(itemView);
          ApplyItemGap(itemView, horizontal, gap, itemIndex == 0);
          listContainer.Add(itemView);
          itemIndex++;
        }
      }

      return listContainer;
    }

    const TreeNode* templateNode = childrenNode->Find("template");
    if(templateNode && templateNode->GetType() == TreeNode::OBJECT)
    {
      const TreeNode* tmplComponents = templateNode->Find("components");
      const TreeNode* tmplBinding = templateNode->Find("dataBinding");

      if(tmplComponents && tmplBinding && tmplBinding->GetType() == TreeNode::STRING)
      {
        std::string dataBindingPath = ctx.Resolve(tmplBinding->GetString());
        const Dali::Ui::Integration::TreeNode* arrayNode = ctx.GetDataModel().ResolvePath(dataBindingPath);

        if(arrayNode && arrayNode->GetType() == TreeNode::ARRAY)
        {
          SurfaceComponentsModel tmplCompModel;
          tmplCompModel.AddComponents(*tmplComponents);

          // Find the root of the template (first component)
          std::string tmplRootId;
          if(tmplComponents->GetType() == TreeNode::ARRAY)
          {
            auto firstIt = tmplComponents->CBegin();
            if(firstIt != tmplComponents->CEnd())
            {
              const TreeNode* idNode = (*firstIt).second.Find("id");
              if(idNode && idNode->GetType() == TreeNode::STRING)
              {
                tmplRootId = idNode->GetString();
              }
            }
          }

          int itemIndex = 0;
          for(auto arrIt = arrayNode->CBegin(); arrIt != arrayNode->CEnd(); ++arrIt)
          {
            DataContext childCtx = ctx.CreateChildContext(dataBindingPath + "/" + std::to_string(itemIndex));

            if(!tmplRootId.empty())
            {
              View itemView = RenderComponent(tmplRootId, tmplCompModel, childCtx);
              sizeItemMainAxis(itemView);
              ApplyItemGap(itemView, horizontal, gap, itemIndex == 0);
              listContainer.Add(itemView);
            }

            itemIndex++;
          }

          return listContainer;
        }
      }
    }
  }

  // Simple list: children is an array of component IDs
  int index = 0;
  for(const auto& childId : comp.childIds)
  {
    View child = RenderComponent(childId, components, ctx);
    sizeItemMainAxis(child);
    ApplyItemGap(child, horizontal, gap, index == 0);
    listContainer.Add(child);
    index++;
  }

  return listContainer;
}
} // namespace A2ui
