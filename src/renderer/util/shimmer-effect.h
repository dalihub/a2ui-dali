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
 * #F8F8FA) sweeps gently from left to right, repeating every 1.2 seconds using
 * an easeInEaseOut interpolation curve. The edges of the light band transition
 * smoothly with no hard edges.
 */

#include <dali-ui-foundation/public-api/views/view.h>
#include <dali/public-api/actors/actor.h>
#include <dali/public-api/adaptor-framework/timer.h>
#include <dali/public-api/animation/animation.h>
#include <dali/public-api/signals/connection-tracker.h>

namespace A2ui {

/**
 * @brief ShimmerEffect creates a shimmer skeleton-screen loading animation on a
 * target View.
 *
 * It creates a clipping container over the target view with a base gray
 * background. Inside the container, a wider gradient overlay View is animated
 * left-to-right, producing a sweeping light band effect. Call Start() to begin
 * and Stop() to end.
 */
class ShimmerEffect : public Dali::ConnectionTracker {
 public:
  /// Base color: slightly dark gray (#E5E5EA)
  static constexpr uint32_t BASE_COLOR = 0xE5E5EA;

  /// Highlight color: light (#F8F8FA)
  static constexpr uint32_t HIGHLIGHT_COLOR = 0xF8F8FA;

  /// Shimmer loop duration in seconds (1.2s, matching the Java reference)
  static constexpr float SHIMMER_DURATION = 1.2f;

  ShimmerEffect();
  ~ShimmerEffect();

  /**
   * @brief Starts the shimmer animation on the target view.
   *
   * Creates a clipping container with base color background, adds a gradient
   * overlay child, and animates the overlay's X position left-to-right in a
   * loop. The overlay creation is deferred by a short timer so the target's
   * layout has completed and its size is known.
   *
   * @param target The view/actor to overlay with the shimmer effect.
   */
  void Start(Dali::Actor target);

  /**
   * @brief Stops the shimmer animation and removes the overlay.
   */
  void Stop();

  /**
   * @brief Returns true if the shimmer animation is currently active.
   */
  bool IsActive() const { return static_cast<bool>(mAnimation); }

 private:
  /// Called by the deferred timer to build the overlay once layout is done.
  bool OnBuildTimer();

  /// Builds the clipping container + gradient overlay + animation.
  void BuildOverlay();

  Dali::Actor mTarget;         ///< The target view being shimmered
  Dali::Ui::View mContainer;   ///< Clipping container with base color
  Dali::Ui::View mGradient;    ///< Gradient overlay that sweeps left-to-right
  Dali::Animation mAnimation;  ///< The looping position animation
  Dali::Timer mBuildTimer;     ///< Defers overlay creation until layout is done
};

}  // namespace A2ui