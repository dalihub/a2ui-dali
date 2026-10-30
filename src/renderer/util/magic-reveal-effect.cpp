/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Magic Reveal — frosted glass lens reveal animation for DALi.
 *
 * Ported from MagicRevealTransition.java:
 *   - Diagonal progressive reveal (top-left → bottom-right)
 *   - Multi-color glass light band with chromatic dispersion
 *   - Light band leads the reveal edge by ~20%
 *   - Image micro-scales from initialScale to 1.0
 *   - Duration: 1.0s, ease-in-out for sweep, decelerate for scale
 *
 * Implementation:
 *   - A clipping container is added as a child of the target, filling its
 * size.
 *   - Inside the container, two overlay Views (larger than the container) are
 *     animated diagonally. The container clips them, so as they move, different
 *     parts of their gradient become visible — creating the sweep effect.
 *     1. Mask overlay: gradient from transparent → placeholder color.
 *     2. Glass overlay: multi-color gradient (warm → white → cool).
 *   - The target itself is micro-scaled from initialScale to 1.0.
 *   - All three animations run simultaneously.
 */

#include "renderer/util/magic-reveal-effect.h"

#include <dali/integration-api/debug.h>
#include <dali/devel-api/actors/actor-devel.h>
#include <dali/public-api/actors/actor-enumerations.h>
#include <dali/public-api/math/vector2.h>
#include <dali/public-api/math/vector3.h>
#include <dali/public-api/math/vector4.h>

#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali-ui-foundation/public-api/gradient/gradient-enumerations.h>
#include <dali-ui-foundation/public-api/gradient/gradient-stop-node.h>
#include <dali-ui-foundation/public-api/gradient/linear-gradient.h>

#include <cmath>

using namespace Dali;

namespace A2ui {

namespace {
/// Helper: create a UiColor from RGB hex + alpha (0..1)
Ui::UiColor MakeColor(uint32_t rgb, float alpha) {
  return Ui::UiColor(rgb, alpha);
}

/// Helper: compute the diagonal length of a rectangle
float DiagonalLength(float w, float h) {
  return std::sqrt(w * w + h * h);
}
}  // namespace

MagicRevealEffect::MagicRevealEffect() {}

MagicRevealEffect::~MagicRevealEffect() {
  Stop();
}

void MagicRevealEffect::Start(Actor target,
                              float durationSec,
                              float glassOpacity,
                              float initialScale) {
  Stop();

  if (!target) {
    return;
  }

  mTarget = target;
  mDurationSec = durationSec > 0.0f ? durationSec : DEFAULT_DURATION;
  mGlassOpacity = glassOpacity;
  mInitialScale = initialScale;

  target.SetProperty(Actor::Property::SCALE,
                     Vector3(mInitialScale, mInitialScale, 1.0f));

  mBuildTimer = Timer::New(100);
  mBuildTimer.TickSignal().Connect(this, &MagicRevealEffect::OnBuildTimer);
  mBuildTimer.Start();
}

bool MagicRevealEffect::OnBuildTimer() {
  BuildOverlay();
  return false;
}

// Key design: overlays are LARGER than the container (by diagonal in each
// direction). The container clips them. Initially, the overlay is positioned
// so the opaque zone covers the entire container. As the overlay moves
// diagonally by +diagonal, the transparent zone sweeps across, revealing
// the image underneath.
//
// This matches the shimmer-effect pattern: FILL_TO_PARENT container,
// SetRequestedX/Y for initial position, animate POSITION.

void MagicRevealEffect::BuildOverlay() {
  if (!mTarget) {
    return;
  }

  Vector3 targetSize = mTarget.GetCurrentSize();
  float width = targetSize.x > 0.0f ? targetSize.x : 300.0f;
  float height = targetSize.y > 0.0f ? targetSize.y : 200.0f;

  float diagonal = DiagonalLength(width, height);

  // Overlay is larger than the container so it can fully cover it at any
  // position
  float overlayW = width + 2.0f * diagonal;
  float overlayH = height + 2.0f * diagonal;

  // ================================================================
  // Clipping container: added as a SIBLING of the target (to the
  // target's parent), positioned to overlap the target exactly.
  //
  // We cannot add it as a child of the target because ImageView's image
  // visual renders on top of child actors, hiding them. By adding the
  // container as a sibling that comes after the target in the parent's
  // child list, it renders on top of the image.
  //
  // The container is FIXED size matching the target, and positioned at
  // the target's position within the parent.
  // ================================================================

  // Get the target's parent so we can add the container as a sibling.
  Actor targetParent = mTarget.GetParent();
  if (!targetParent) {
    return;
  }

  // Get the target's position within its parent so we can place the
  // container at the same location.
  Vector3 targetPos = mTarget.GetCurrentPosition();

  mContainer = Ui::View::New();
  mContainer.SetParentOrigin(ParentOrigin::TOP_LEFT);
  // STANDALONE: exclude from the parent's layout system (e.g. FlexLayout)
  // so the parent doesn't override our position. We position manually.
  mContainer.SetLayoutMode(Ui::LayoutMode::STANDALONE);
  DevelActor::SetResizePolicy(mContainer, ResizePolicy::FIXED, Dimension::ALL_DIMENSIONS);
  mContainer.SetRequestedWidth(width);
  mContainer.SetRequestedHeight(height);
  mContainer.SetRequestedX(targetPos.x);
  mContainer.SetRequestedY(targetPos.y);
  mContainer.SetProperty(Actor::Property::POSITION,
                         Vector3(targetPos.x, targetPos.y, 0.0f));
  mContainer.SetProperty(Actor::Property::CLIPPING_MODE,
                         ClippingMode::CLIP_TO_BOUNDING_BOX);

  // ================================================================
  // Mask overlay: larger than container, diagonal gradient.
  // Gradient: top-left = transparent, bottom-right = opaque placeholder.
  // Initial position: (-diagonal, -diagonal) → the opaque zone covers
  // the container entirely (image is hidden).
  // Final position: (+diagonal, +diagonal) → the transparent zone covers
  // the container entirely (image is revealed).
  // ================================================================
  mMaskOverlay = Ui::View::New();
  mMaskOverlay.SetParentOrigin(ParentOrigin::TOP_LEFT);
  DevelActor::SetResizePolicy(mMaskOverlay, ResizePolicy::FIXED, Dimension::ALL_DIMENSIONS);
  mMaskOverlay.SetRequestedWidth(overlayW);
  mMaskOverlay.SetRequestedHeight(overlayH);

  {
    Ui::Gradient::Linear gradient(Vector2(-0.5f, -0.5f), Vector2(0.5f, 0.5f));
    gradient.SetUnits(Ui::Gradient::Units::OBJECT_BOUNDING_BOX);
    gradient.SetSpreadMethod(Ui::Gradient::SpreadMethod::PAD);

    // left = transparent (revealed), right = opaque (unrevealed)
    Dali::Vector<Ui::Gradient::StopNode> stopNodes;
    stopNodes.PushBack(Ui::Gradient::StopNode(0.0f, MakeColor(0x000000, 0.0f)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.35f, MakeColor(0x000000, 0.0f)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.55f, MakeColor(PLACEHOLDER_COLOR, 1.0f)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(1.0f, MakeColor(PLACEHOLDER_COLOR, 1.0f)));
    gradient.SetStopNodes(stopNodes);

    mMaskOverlay.SetBackgroundGradient(gradient);
  }

  // Initial position: (-diagonal, -diagonal) so opaque zone covers container
  float maskInitX = -diagonal;
  float maskInitY = -diagonal;
  mMaskOverlay.SetRequestedX(maskInitX);
  mMaskOverlay.SetRequestedY(maskInitY);
  mMaskOverlay.SetProperty(Actor::Property::POSITION,
                           Vector3(maskInitX, maskInitY, 0.1f));

  // ================================================================
  // Glass overlay: same size, multi-color light band.
  // Starts shifted back by 20% of diagonal so the light band leads the mask.
  // ================================================================
  mGlassOverlay = Ui::View::New();
  mGlassOverlay.SetParentOrigin(ParentOrigin::TOP_LEFT);
  DevelActor::SetResizePolicy(mGlassOverlay, ResizePolicy::FIXED, Dimension::ALL_DIMENSIONS);
  mGlassOverlay.SetRequestedWidth(overlayW);
  mGlassOverlay.SetRequestedHeight(overlayH);

  {
    Ui::Gradient::Linear gradient(Vector2(-0.5f, -0.5f), Vector2(0.5f, 0.5f));
    gradient.SetUnits(Ui::Gradient::Units::OBJECT_BOUNDING_BOX);
    gradient.SetSpreadMethod(Ui::Gradient::SpreadMethod::PAD);

    float warmAlpha = mGlassOpacity * 0.35f;
    float peakAlpha = mGlassOpacity;
    float coolAlpha = mGlassOpacity * 0.30f;
    float subtleAlpha = mGlassOpacity * 0.12f;

    // Light band at ~30-50% of the gradient, rest transparent
    Dali::Vector<Ui::Gradient::StopNode> stopNodes;
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.00f, MakeColor(0x000000, 0.0f)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.25f, MakeColor(0x000000, 0.0f)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.30f, MakeColor(0xFFFFFF, subtleAlpha)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.35f, MakeColor(0xFFF2E0, warmAlpha)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.40f, MakeColor(0xFFFFFF, peakAlpha)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.45f, MakeColor(0xD9EBFF, coolAlpha)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.50f, MakeColor(0xFFFFFF, subtleAlpha)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(0.55f, MakeColor(0x000000, 0.0f)));
    stopNodes.PushBack(
        Ui::Gradient::StopNode(1.00f, MakeColor(0x000000, 0.0f)));
    gradient.SetStopNodes(stopNodes);

    mGlassOverlay.SetBackgroundGradient(gradient);
  }

  // Glass starts 20% behind so the light band leads the mask edge
  float glassOffset = diagonal * 0.2f;
  float glassInitX = maskInitX - glassOffset;
  float glassInitY = maskInitY - glassOffset;
  mGlassOverlay.SetRequestedX(glassInitX);
  mGlassOverlay.SetRequestedY(glassInitY);
  mGlassOverlay.SetProperty(Actor::Property::POSITION,
                            Vector3(glassInitX, glassInitY, 0.2f));

  // ================================================================
  // Assemble: add overlays to container, then container as a sibling
  // of the target (added to the target's parent so it renders on top)
  // ================================================================
  mContainer.Add(mMaskOverlay);
  mContainer.Add(mGlassOverlay);
  targetParent.Add(mContainer);

  // ================================================================
  // Animation
  // ================================================================
  mAnimation = Animation::New(mDurationSec);

  // Mask: move from (-diag, -diag) to (+diag, +diag)
  mAnimation.AnimateTo(Property(mMaskOverlay, Actor::Property::POSITION),
                       Vector3(diagonal, diagonal, 0.1f),
                       AlphaFunction::EASE_IN_OUT_SINE);

  // Glass: move from (-diag-offset, -diag-offset) to (+diag-offset,
  // +diag-offset)
  mAnimation.AnimateTo(
      Property(mGlassOverlay, Actor::Property::POSITION),
      Vector3(diagonal - glassOffset, diagonal - glassOffset, 0.2f),
      AlphaFunction::EASE_IN_OUT_SINE);

  // Scale: decelerate from initialScale to 1.0
  mAnimation.AnimateTo(Property(mTarget, Actor::Property::SCALE),
                       Vector3(1.0f, 1.0f, 1.0f), AlphaFunction::EASE_OUT);

  mAnimation.FinishedSignal().Connect(this,
                                      &MagicRevealEffect::OnAnimationFinish);
  mAnimation.SetEndAction(Animation::BAKE);
  mAnimation.SetDisconnectAction(Animation::DISCARD);
  mAnimation.Play();
}

void MagicRevealEffect::OnAnimationFinish(Dali::Animation animation) {
  if (mTarget) {
    mTarget.SetProperty(Actor::Property::SCALE, Vector3(1.0f, 1.0f, 1.0f));
  }

  if (mContainer) {
    Actor parent = mContainer.GetParent();
    if (parent) {
      parent.Remove(mContainer);
    }
    mContainer.Reset();
  }

  if (mMaskOverlay) {
    mMaskOverlay.Reset();
  }

  if (mGlassOverlay) {
    mGlassOverlay.Reset();
  }

  if (mAnimation) {
    mAnimation.Clear();
    mAnimation.Reset();
  }

  mTarget.Reset();
}

void MagicRevealEffect::Stop() {
  if (mBuildTimer) {
    mBuildTimer.Stop();
    mBuildTimer.Reset();
  }

  if (mAnimation) {
    mAnimation.Stop();
    mAnimation.Clear();
    mAnimation.Reset();
  }

  if (mTarget) {
    mTarget.SetProperty(Actor::Property::SCALE, Vector3(1.0f, 1.0f, 1.0f));
  }

  if (mContainer) {
    Actor parent = mContainer.GetParent();
    if (parent) {
      parent.Remove(mContainer);
    }
    mContainer.Reset();
  }

  if (mMaskOverlay) {
    mMaskOverlay.Reset();
  }

  if (mGlassOverlay) {
    mGlassOverlay.Reset();
  }

  mTarget.Reset();
}

}  // namespace A2ui