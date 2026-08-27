#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Shimmer skeleton-screen loading animation for DALi.
 *
 * Effect description (ported from ShimmerTransition.java):
 * The image area displays a solid placeholder with a slightly dark gray color
 * (#E5E5EA). On top of this base color, a soft light band (highlight color
 * #F8F8FA) sweeps gently from left to right, repeating every 0.6 seconds using
 * an easeInEaseOut interpolation curve. The edges of the light band transition
 * smoothly with no hard edges.
 */

#include <dali-ui-foundation/public-api/views/view.h>
#include <dali/public-api/animation/animation.h>

namespace A2ui {

/**
 * @brief ShimmerEffect creates a shimmer skeleton-screen loading animation.
 *
 * BuildOverlay() returns a self-contained View with a looping shimmer
 * animation already playing. The caller adds the returned View to the
 * scene graph wherever a shimmer placeholder is needed.
 */
class ShimmerEffect {
 public:
  /// Base color: slightly dark gray (#E5E5EA)
  static constexpr uint32_t BASE_COLOR = 0xE5E5EA;

  /// Highlight color: light (#F8F8FA)
  static constexpr uint32_t HIGHLIGHT_COLOR = 0xF8F8FA;

  /// Shimmer loop duration in seconds (0.6s, matching Android ShimmerTransition)
  static constexpr float SHIMMER_DURATION = 0.6f;

  /**
   * @brief Builds a self-contained shimmer View with a looping animation.
   *
   * Creates a clipping container with the base gray background, adds a
   * gradient overlay child, and starts a looping left-to-right sweep
   * animation. The animation plays indefinitely.
   *
   * @param width  The width of the shimmer placeholder in pixels.
   * @param height The height of the shimmer placeholder in pixels.
   * @return A View containing the shimmer animation, ready to be added to
   *         the scene graph.
   */
  static Dali::Ui::View BuildOverlay(float width, float height);
};

}  // namespace A2ui
