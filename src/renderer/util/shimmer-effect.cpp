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
 *   - Sweeps left-to-right, 1.2s loop, easeInEaseOut
 *   - Smooth gradient edges, no hard edges
 *
 * Implementation: Uses DALi's gradient API (LinearGradient) on a wide overlay
 * View that is clipped by a container and animated left-to-right via Animation.
 * The Start() method defers creation to a 100ms timer so the target View has
 * been laid out and its size is known.
 */

#include "renderer/util/shimmer-effect.h"

#include <dali/integration-api/debug.h>
#include <dali/devel-api/actors/actor-devel.h>
#include <dali/public-api/actors/actor-enumerations.h>
#include <dali/public-api/math/vector2.h>

#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali-ui-foundation/public-api/gradient/gradient-enumerations.h>
#include <dali-ui-foundation/public-api/gradient/gradient-stop-node.h>
#include <dali-ui-foundation/public-api/gradient/linear-gradient.h>
#include <dali-ui-foundation/public-api/visuals/gradient-visual.h>

using namespace Dali;

namespace A2ui {

ShimmerEffect::ShimmerEffect() {}

ShimmerEffect::~ShimmerEffect() {
  Stop();
}

void ShimmerEffect::Start(Actor target) {
  // Remove any existing shimmer
  Stop();

  if (!target) {
    return;
  }

  mTarget = target;

  // Defer the overlay creation by 100ms so the target's layout has completed
  // and GetCurrentSize() returns the real size.
  mBuildTimer = Timer::New(100);
  mBuildTimer.TickSignal().Connect(this, &ShimmerEffect::OnBuildTimer);
  mBuildTimer.Start();
}

bool ShimmerEffect::OnBuildTimer() {
  BuildOverlay();
  // One-shot: stop the timer
  return false;
}

void ShimmerEffect::BuildOverlay() {
  if (!mTarget) {
    return;
  }

  Vector3 targetSize = mTarget.GetCurrentSize();
  float width = targetSize.x > 0.0f ? targetSize.x : 300.0f;
  float height = targetSize.y > 0.0f ? targetSize.y : 200.0f;

  DALI_LOG_ERROR("[A2UI][shimmer] width=%.2f height=%.2f\n", width, height);

  // Key insight: Ui::View's layout system controls child size/position.
  // Must use SetRequestedWidth/Height (not Actor::Property::SIZE) for the
  // layout system to give children proper dimensions.

  // Create the clipping container: fills the parent, clips children to its
  // bounds, and has the base gray background color.
  mContainer = Dali::Ui::View::New();
  mContainer.SetParentOrigin(ParentOrigin::TOP_LEFT);
  DevelActor::SetResizePolicy(mContainer, ResizePolicy::FILL_TO_PARENT,
                             Dimension::ALL_DIMENSIONS);
  mContainer.SetBackgroundColor(Dali::Ui::UiColor(BASE_COLOR));
  mContainer.SetProperty(Actor::Property::CLIPPING_MODE,
                         ClippingMode::CLIP_TO_BOUNDING_BOX);

  // Create the gradient overlay — FIXED size matching the container.
  // FIXED policy lets us animate POSITION without the layout system overriding
  // it. REPEAT spread makes the gradient tile so shifting by -width looks
  // continuous.
  mGradient = Dali::Ui::View::New();
  mGradient.SetParentOrigin(ParentOrigin::TOP_LEFT);
  DevelActor::SetResizePolicy(mGradient, ResizePolicy::FIXED, Dimension::ALL_DIMENSIONS);
  mGradient.SetRequestedWidth(width);
  mGradient.SetRequestedHeight(height);

  // Build the linear gradient: left-to-right, REPEAT spread.
  // OBJECT_BOUNDING_BOX: top-left=(-0.5,-0.5), bottom-right=(0.5,0.5).
  Dali::Ui::Gradient::Linear gradient(
      Vector2(-0.5f, 0.0f),  // start (left edge)
      Vector2(0.5f, 0.0f)    // end (right edge)
  );
  gradient.SetUnits(Dali::Ui::Gradient::Units::OBJECT_BOUNDING_BOX);
  gradient.SetSpreadMethod(Dali::Ui::Gradient::SpreadMethod::PAD);

  // Stop nodes: highlight at the LEFT edge (position 0), fading to base color.
  // As the view animates right (0 → width), the highlight sweeps left-to-right.
  Dali::Vector<Dali::Ui::Gradient::StopNode> stopNodes;
  stopNodes.PushBack(
      Dali::Ui::Gradient::StopNode(0.0f, Dali::Ui::UiColor(HIGHLIGHT_COLOR)));
  stopNodes.PushBack(
      Dali::Ui::Gradient::StopNode(0.15f, Dali::Ui::UiColor(BASE_COLOR)));
  stopNodes.PushBack(
      Dali::Ui::Gradient::StopNode(0.5f, Dali::Ui::UiColor(BASE_COLOR)));
  stopNodes.PushBack(
      Dali::Ui::Gradient::StopNode(1.0f, Dali::Ui::UiColor(BASE_COLOR)));

  gradient.SetStopNodes(stopNodes);

  mGradient.SetBackgroundGradient(gradient);

  // Add gradient as child of the clipping container
  mContainer.Add(mGradient);

  // Add container as child of the target
  mTarget.Add(mContainer);

  // Set initial position via layout API so the layout system places it at
  // top-left
  mGradient.SetRequestedX(0.0f);
  mGradient.SetRequestedY(0.0f);
  // Also set Actor property for the animation baseline
  mGradient.SetProperty(Actor::Property::POSITION, Vector3(0.0f, 0.0f, 0.1f));

  // Animate POSITION from 0 to +width. With REPEAT spread, this shifts the
  // gradient pattern right by one tile, making the highlight band sweep
  // from left to right across the view.
  mAnimation = Animation::New(SHIMMER_DURATION);
  mAnimation.AnimateTo(Property(mGradient, Actor::Property::POSITION),
                       Vector3(width, 0.0f, 0.1f),
                       AlphaFunction::EASE_IN_OUT_SINE);

  mAnimation.SetLooping(true);
  mAnimation.SetLoopingMode(Animation::RESTART);
  mAnimation.SetEndAction(Animation::BAKE);
  mAnimation.SetDisconnectAction(Animation::DISCARD);
  mAnimation.Play();
}

void ShimmerEffect::Stop() {
  // Stop the build timer if still pending
  if (mBuildTimer) {
    mBuildTimer.Stop();
    mBuildTimer.Reset();
  }

  if (mAnimation) {
    mAnimation.Stop();
    mAnimation.Clear();
    mAnimation.Reset();
  }

  if (mContainer) {
    Actor parent = mContainer.GetParent();
    if (parent) {
      parent.Remove(mContainer);
    }
    mContainer.Reset();
  }

  if (mGradient) {
    mGradient.Reset();
  }

  mTarget.Reset();
}

}  // namespace A2ui