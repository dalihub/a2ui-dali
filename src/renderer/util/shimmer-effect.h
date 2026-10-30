#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Shimmer skeleton-screen loading animation for DALi.
 *
 * Effect description (ported from ShimmerTransition.java):
 * The image area displays a solid placeholder with a slightly dark gray color
 * (#3A3A3D). On top of this base color, a soft light band (highlight color
 * #636368) sweeps gently from left to right, repeating every 1.5 seconds using
 * an easeInEaseOut interpolation curve. The edges of the light band transition
 * smoothly with no hard edges.
 *
 * The container uses WRAP_CONTENT + FlexGrow(1.0) so it fills the parent's
 * remaining main-axis space without overflowing or pushing siblings off-screen.
 * The gradient overlay uses a fixed large width (1920px) so the shimmer sweep
 * covers any screen width. The container's CLIP_TO_BOUNDING_BOX clips the
 * gradient to the actual visible area.
 */

#include <dali-ui-foundation/public-api/views/view.h>
#include <dali/public-api/animation/animation.h>

namespace A2ui {

/**
 * @brief ShimmerEffect creates a shimmer skeleton-screen loading animation.
 *
 * BuildOverlay() returns a self-contained View and its looping shimmer
 * animation, not yet playing. The caller adds the View to the scene graph
 * wherever a shimmer placeholder is needed and decides when it plays.
 *
 * The returned View uses WRAP_CONTENT + FlexGrow(1.0) so it fills the
 * parent's remaining main-axis space. The gradient overlay uses a fixed
 * 1920px width so the sweep covers any screen size.
 */
class ShimmerEffect {
 public:
  /// Base color: OneUI SurfaceContainer (dark) (#3A3A3D)
  static constexpr uint32_t BASE_COLOR = 0x3A3A3D;

  /// Highlight color: OneUI Outline (dark) — lighter for sweep band (#636368)
  static constexpr uint32_t HIGHLIGHT_COLOR = 0x636368;

  /// Shimmer loop duration in seconds
  static constexpr float SHIMMER_DURATION = 1.5f;

  /**
   * @brief Builds a self-contained shimmer View with a looping animation.
   *
   * Creates a clipping container with WRAP_CONTENT + FlexGrow(1.0) and the
   * base gray background. A gradient overlay (fixed 1920px width) is added
   * as a child and animated left-to-right with a looping sweep animation.
   * The container's CLIP_TO_BOUNDING_BOX clips the gradient to the actual
   * visible area.
   *
   * @param[out] animation The looping sweep animation (not started).
   * @return A View containing the shimmer animation, ready to be added to
   *         the scene graph.
   */
  static Dali::Ui::View BuildOverlay(Dali::Animation& animation);
};

}  // namespace A2ui
