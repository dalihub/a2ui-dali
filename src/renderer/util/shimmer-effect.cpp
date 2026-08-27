/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Shimmer skeleton-screen loading animation for DALi.
 *
 * Ported from ShimmerTransition.java:
 *   - Base color: #E5E5EA (slightly dark gray)
 *   - Highlight color: #F8F8FA (light)
 *   - Light band width ~30% of view width
 *   - Sweeps left-to-right, 0.6s loop, easeInEaseOut
 *   - Smooth gradient edges, no hard edges
 *
 * Implementation: Uses DALi's gradient API (LinearGradient) on a wide overlay
 * View that is clipped by a container and animated left-to-right via Animation.
 * BuildOverlay() returns a self-contained View with the looping animation
 * already playing.
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

using namespace Dali;

namespace A2ui {

// Android ShimmerTransition port: gradient domain is -0.3W ~ +1.3W,
// total width = 1.6 × container width. The symmetric highlight band
// (centered at View 50%) sweeps from left to right.
//
// Coordinate mapping: Android's LinearGradient starts at -shimmerWidth;
// DALi's gradient maps to the View's left edge (0). So we offset the
// DALi View position by -shimmerWidth to align:
//   DALi View X = Android translationX - shimmerWidth
//   start: -0.3W - 0.3W = -0.6W
//   end:   1.0W - 0.3W =  0.7W
//   distance = 1.3W  (matches Android)
Ui::View ShimmerEffect::BuildOverlay(float width, float height) {
  // Create the clipping container: fills the parent, clips children to its
  // bounds, and has the base gray background color.
  Ui::View container = Ui::View::New();
  container.SetParentOrigin(ParentOrigin::TOP_LEFT);
  container.SetRequestedWidth(width);
  container.SetRequestedHeight(height);
  container.SetBackgroundColor(Ui::UiColor(BASE_COLOR, 0.5f));
  container.SetProperty(Actor::Property::CLIPPING_MODE,
                         ClippingMode::CLIP_TO_BOUNDING_BOX);

  // Create the gradient overlay — FIXED size matching the container.
  // FIXED policy lets us animate POSITION without the layout system overriding
  // it. PAD spread keeps the gradient edges clean.
  Ui::View gradient = Ui::View::New();
  gradient.SetParentOrigin(ParentOrigin::TOP_LEFT);
  DevelActor::SetResizePolicy(gradient, ResizePolicy::FIXED, Dimension::ALL_DIMENSIONS);

  float shimmerWidth = width * 0.3f;
  float gradientWidth = width * 1.6f;  // 1.0W + 2×0.3W
  float startPos = -shimmerWidth * 2.0f;  // -0.6W
  float endPos = width - shimmerWidth;     //  0.7W
  gradient.SetRequestedWidth(gradientWidth);
  gradient.SetRequestedHeight(height);

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
  //   0.35  → transparent       (front segment invisible)
  //   0.50  → HIGHLIGHT_COLOR   (center peak, bright band)
  //   0.65  → transparent       (back segment invisible)
  //   1.00  → transparent       (View right edge)
  // Bright band width = (0.65-0.35) × 1.6W = 0.48W
  // Both side gradients = 0.35 × 1.6W = 0.56W, forming symmetric transparent→bright→transparent
  Vector<Ui::Gradient::StopNode> stopNodes;
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.00f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.35f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.50f,  Ui::UiColor(HIGHLIGHT_COLOR)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(0.65f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));
  stopNodes.PushBack(
      Ui::Gradient::StopNode(1.00f,  Ui::UiColor(0.0f, 0.0f, 0.0f, 0.0f)));

  grad.SetStopNodes(stopNodes);
  gradient.SetBackgroundGradient(grad);

  // Add gradient as child of the clipping container
  container.Add(gradient);

  // Android ShimmerTransition port: View starts at startPos (-0.6W) and
  // animates to endPos (+0.7W). The symmetric highlight band sweeps
  // left-to-right with AccelerateDecelerate (ease in-out) interpolation.
  // Animation distance = 1.3 × width (matches Android exactly).
  gradient.SetRequestedX(startPos);
  gradient.SetRequestedY(0.0f);
  // Also set Actor property for the animation baseline
  gradient.SetProperty(Actor::Property::POSITION,
                        Vector3(startPos, 0.0f, 0.1f));

  // Animate POSITION from startPos to endPos.
  // EASE_IN_OUT matches Android's AccelerateDecelerateInterpolator.
  Animation animation = Animation::New(SHIMMER_DURATION);
  animation.AnimateTo(Property(gradient, Actor::Property::POSITION),
                       Vector3(endPos, 0.0f, 0.1f),
                       AlphaFunction::EASE_IN_OUT);
  animation.SetLooping(true);
  animation.SetLoopingMode(Animation::RESTART);
  animation.SetEndAction(Animation::BAKE);
  animation.SetDisconnectAction(Animation::DISCARD);
  animation.Play();

  return container;
}

}  // namespace A2ui

