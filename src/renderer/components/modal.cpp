#include "renderer/render-internal.h"

namespace A2ui
{

View A2uiRenderer::RenderModal(const ComponentModel& comp,
                               const SurfaceComponentsModel& components,
                               DataContext& ctx)
{
  if(!comp.rawNode) return View::New();

  // A2UI Modal `trigger` is a *reference* to a component that is already rendered in the
  // layout (e.g. the movie card lists watch-trailer-btn in its content column AND names it
  // as the modal trigger) — re-rendering it here produced a duplicate button. So the Modal
  // renders only its overlay content, hidden until opened, and wires the open tap onto the
  // already-rendered trigger view by id (if present) instead of drawing a second copy.
  FlexLayout container = FlexLayout::New();
  container.SetDirection(FlexDirection::COLUMN);
  container.SetRequestedWidth(MATCH_PARENT);
  container.SetRequestedHeight(WRAP_CONTENT);

  FlexLayout modalCard = FlexLayout::New();
  modalCard.SetDirection(FlexDirection::COLUMN);
  modalCard.SetRequestedWidth(MATCH_PARENT);
  // Collapsed to zero height while hidden — a VISIBLE=false FlexLayout still reserves its
  // WRAP_CONTENT height, which left a tall empty gap under cards with a hidden modal (e.g.
  // the movie card). An open handler restores it to WRAP_CONTENT.
  modalCard.SetRequestedHeight(0.0f);
  modalCard.SetBackgroundColor(COLOR_CARD_BG);
  modalCard.SetCornerRadius(Metrics::RadiusCard());
  modalCard.SetBorderlineWidth(Metrics::BorderCard());
  modalCard.SetBorderlineColor(A2uiTheme::Color("Outline"));
  modalCard.SetPadding(Extents(static_cast<uint16_t>(Metrics::Dp(20)), static_cast<uint16_t>(Metrics::Dp(20)),
                               static_cast<uint16_t>(Metrics::Dp(16)), static_cast<uint16_t>(Metrics::Dp(16))));
  modalCard.SetMargin(Extents(0, 0, static_cast<uint16_t>(Metrics::Dp(8)), 0));
  modalCard.SetProperty(Actor::Property::VISIBLE, false);

  // Close (X) row.
  FlexLayout closeRow = FlexLayout::New();
  closeRow.SetDirection(FlexDirection::ROW);
  closeRow.SetJustifyContent(FlexJustify::FLEX_END);
  closeRow.SetRequestedWidth(MATCH_PARENT);
  closeRow.SetRequestedHeight(Metrics::Dp(24));
  Label closeLabel = Label::New("X");
  closeLabel.SetFontSize(Metrics::FontInput());
  closeLabel.SetTextColor(A2uiTheme::Color("OnSurfaceContainerLow"));
  closeLabel.SetRequestedWidth(Metrics::Dp(24));
  closeLabel.SetRequestedHeight(Metrics::Dp(24));
  closeRow.Add(closeLabel);
  {
    // Close → hide AND re-collapse to zero height (so the hidden overlay reserves no space).
    // One lambda drives both touch and the remote OK key.
    auto closeModal = [modalCard]() mutable {
      modalCard.SetProperty(Actor::Property::VISIBLE, false);
      modalCard.SetRequestedHeight(0.0f);
    };
    Dali::TapGestureDetector closeDet = Dali::TapGestureDetector::New();
    closeDet.Attach(closeRow);
    closeDet.DetectedSignal().Connect(this,
      [closeModal](Dali::Actor, const Dali::TapGesture&) mutable { closeModal(); });
    RetainTapDetector(closeDet);

    // TV remote: the close (X) row is a focus target; OK/Enter closes the modal.
    EnableKeyActivation(closeRow, [closeModal]() mutable { closeModal(); });
  }
  modalCard.Add(closeRow);

  // Content (accept "content" or v0.8 "contentChild").
  const TreeNode* contentNode = comp.rawNode->Find("content");
  if(!contentNode) contentNode = comp.rawNode->Find("contentChild");
  if(contentNode && contentNode->GetType() == TreeNode::STRING)
  {
    View content = RenderComponent(contentNode->GetString(), components, ctx);
    if(content) modalCard.Add(content);
  }
  container.Add(modalCard);

  // Open path: the trigger is a sibling already rendered earlier in the surface (the column
  // lists it before the Modal), so look it up by id in the view map and attach a tap that
  // reveals + expands the overlay. No duplicate trigger is drawn. If the trigger hasn't been
  // rendered yet (declared after the Modal), the overlay simply stays collapsed.
  const TreeNode* triggerNode = comp.rawNode->Find("trigger");
  if(!triggerNode) triggerNode = comp.rawNode->Find("entryPointChild");
  if(triggerNode && triggerNode->GetType() == TreeNode::STRING)
  {
    View triggerView = mDiffEngine.GetView(triggerNode->GetString());
    if(triggerView)
    {
      // One lambda drives both touch and the remote OK key.
      auto openModal = [modalCard]() mutable {
        modalCard.SetProperty(Actor::Property::VISIBLE, true);
        modalCard.SetRequestedHeight(WRAP_CONTENT);
      };
      Dali::TapGestureDetector openDet = Dali::TapGestureDetector::New();
      openDet.Attach(triggerView);
      openDet.DetectedSignal().Connect(this,
        [openModal](Dali::Actor, const Dali::TapGesture&) mutable { openModal(); });
      RetainTapDetector(openDet);

      // TV remote: focus the trigger; OK/Enter opens the modal.
      EnableKeyActivation(triggerView, [openModal]() mutable { openModal(); });
    }
  }

  return container;
}
} // namespace A2ui
