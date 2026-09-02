/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Shimmer skeleton-screen loading animation for DALi.
 *
 * Ported from ShimmerTransition.java:
 *   - Base color: #E5E5EA (slightly dark gray)
 *   - Highlight color: #F8F8FA (light)
 *   - Light band sweeps left-to-right, 0.6s loop, easeInEaseOut
 *   - Smooth gradient edges, no hard edges
 *
 * Implementation: Uses DALi's gradient API (LinearGradient) on a wide overlay
 * View that is clipped by a container and animated left-to-right via Animation.
 * BuildOverlay() returns a self-contained View with the looping animation
 * already playing.
 *
 * The container uses WRAP_CONTENT + FlexGrow(1.0) so it fills the parent's
 * remaining main-axis space without overflowing or pushing siblings off-screen.
 * The gradient overlay uses a fixed large width (1920px) so the shimmer sweep
 * covers any screen width. The container's CLIP_TO_BOUNDING_BOX clips the
 * gradient to the actual visible area.
 */

#include "renderer/util/shimmer-effect.h"

#include <dali/devel-api/actors/actor-devel.h>
#include <dali/public-api/actors/actor-enumerations.h>
#include <dali/public-api/math/vector2.h>
#include <dali/public-api/math/vector3.h>

#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali-ui-foundation/public-api/gradient/gradient-enumerations.h>
#include <dali-ui-foundation/public-api/gradient/gradient-stop-node.h>
#include <dali-ui-foundation/public-api/gradient/linear-gradient.h>
#include <dali-ui-foundation/public-api/visuals/gradient-visual.h>
#include <dali-ui-foundation/public-api/layouts/flex-layout-params.h>

using namespace Dali;

namespace A2ui {

// Fixed reference width for the gradient overlay. 1920px covers any practical
// screen width; the container clips the gradient to its actual bounds.
static constexpr float SHIMMER_REF_WIDTH = 1920.0f;

Ui::View ShimmerEffect::BuildOverlay() {
  // Create the clipping container: WRAP_CONTENT + FlexGrow fills the parent's
  // remaining main-axis space without overflowing or pushing siblings.
  // CLIP_TO_BOUNDING_BOX clips the gradient child to the container's bounds.
  Ui::View container = Ui::View::New();
  container.SetParentOrigin(ParentOrigin::TOP_LEFT);
  container.SetRequestedWidth(-1.0f);   // WRAP_CONTENT
  container.SetRequestedHeight(-1.0f);   // WRAP_CONTENT
  container.SetBackgroundColor(Ui::UiColor(ShimmerEffect::BASE_COLOR, 0.5f));
  container.SetProperty(Actor::Property::CLIPPING_MODE,
                         ClippingMode::CLIP_TO_BOUNDING_BOX);
  // FlexGrow(1.0) + FlexShrink(1.0): fill remaining main-axis space, shrink if needed
  container.SetLayoutParams(
    Dali::Ui::FlexLayoutParams::New()
      .SetFlexGrow(1.0f)
      .SetFlexShrink(1.0f)
  );

  // Create the gradient overlay — FIXED size based on a reference width.
  // FIXED policy lets us animate POSITION without the layout system overriding it.
  Ui::View gradient = Ui::View::New();
  gradient.SetParentOrigin(ParentOrigin::TOP_LEFT);
  DevelActor::SetResizePolicy(gradient, ResizePolicy::FIXED, Dimension::ALL_DIMENSIONS);

  gradient.SetRequestedWidth(SHIMMER_REF_WIDTH);
  gradient.SetRequestedHeight(20.0f);  // fixed height matching typical text row

  // Build the linear gradient: left-to-right, PAD spread.
  // OBJECT_BOUNDING_BOX: top-left=(-0.5,-0.5), bottom-right=(0.5,0.5).
  Ui::Gradient::Linear grad(
      Vector2(-0.5f, 0.0f),  // start (left edge)
      Vector2(0.5f, 0.0f)    // end (right edge)
  );
  grad.SetUnits(Ui::Gradient::Units::OBJECT_BOUNDING_BOX);
  grad.SetSpreadMethod(Ui::Gradient::SpreadMethod::PAD);

  // Transparent shimmer: the gradient is fully transparent except for the
  // highlight band, so only the sweeping light is visible over content.
  //   0.00  → transparent       (View left edge)
  //   0.15  → transparent       (front segment invisible)
  //   0.25  → HIGHLIGHT_COLOR   (center peak, bright band)
  //   0.35  → transparent       (back segment invisible)
  //   1.00  → transparent       (View right edge)
  // Bright band width = (0.35-0.15) × 1920 = 384px
  Vector<Ui::Gradient::StopNode> stopNodes;
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.00f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.15f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.25f,  Ui::UiColor(ShimmerEffect::HIGHLIGHT_COLOR)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.35f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(1.00f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));

  grad.SetStopNodes(stopNodes);
  gradient.SetBackgroundGradient(grad);

  // Add gradient as child of the clipping container
  container.Add(gradient);

  // Animate POSITION from 0 to SHIMMER_REF_WIDTH (left-to-right sweep).
  // The container's CLIP_TO_BOUNDING_BOX clips the gradient to the visible area,
  // so the highlight band appears to sweep from left to right across the container.
  gradient.SetRequestedX(0.0f);
  gradient.SetRequestedY(0.0f);
  gradient.SetProperty(Actor::Property::POSITION,
                        Vector3(0.0f, 0.0f, 0.1f));

  // EASE_IN_OUT matches Android's AccelerateDecelerateInterpolator.
  Animation animation = Animation::New(ShimmerEffect::SHIMMER_DURATION);
  animation.AnimateTo(Property(gradient, Actor::Property::POSITION),
                       Vector3(SHIMMER_REF_WIDTH, 0.0f, 0.1f),
                       AlphaFunction::EASE_IN_OUT);
  animation.SetLooping(true);
  animation.SetLoopingMode(Animation::RESTART);
  animation.SetEndAction(Animation::BAKE);
  animation.SetDisconnectAction(Animation::DISCARD);
  animation.Play();

  return container;
}

}  // namespace A2ui

