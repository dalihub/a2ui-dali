#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Magic Reveal — frosted glass lens reveal animation for DALi.
 *
 * Effect inspired by iOS 26 Liquid Glass:
 * 1. A gradient mask controls a diagonal progressive reveal of the image
 *    (top-left → bottom-right).
 * 2. A multi-color light band simulating frosted glass refraction is overlaid
 *    on the parent container.
 * 3. The light band slightly leads the reveal edge, creating a
 *    "lens first, content follows" effect.
 * 4. Throughout the animation, the image micro-scales from 0.98 to 1.0 for a
 *    "floating up" 3D feel.
 *
 * Color composition (from revealed side → unrevealed side):
 *   Transparent → light warm white → bright white → light cool blue →
 * Transparent This warm-to-cool color separation simulates the chromatic
 * dispersion of real glass.
 *
 * Ported from MagicRevealTransition.java.
 */

#include <dali-ui-foundation/public-api/views/view.h>
#include <dali/public-api/actors/actor.h>
#include <dali/public-api/adaptor-framework/timer.h>
#include <dali/public-api/animation/animation.h>
#include <dali/public-api/signals/connection-tracker.h>

namespace A2ui {

/**
 * @brief MagicRevealEffect creates a frosted glass lens reveal animation on a
 * target View.
 *
 * It overlays a diagonal sweep mask and a multi-color glass light band on top
 * of the target view, while micro-scaling the target from initialScale to 1.0.
 * Call Start() to begin the one-shot reveal animation and Stop() to cancel
 * early.
 */
class MagicRevealEffect : public Dali::ConnectionTracker {
 public:
  /// Default animation duration in seconds (1.0s, matching the Java reference)
  static constexpr float DEFAULT_DURATION = 2.0f;

  /// Default glass opacity (0.5)
  static constexpr float DEFAULT_GLASS_OPACITY = 0.5f;

  /// Default initial scale (0.98)
  static constexpr float DEFAULT_INITIAL_SCALE = 0.98f;

  /// Placeholder color for the unrevealed area (#F2F2F7)
  static constexpr uint32_t PLACEHOLDER_COLOR = 0xF2F2F7;

  MagicRevealEffect();
  ~MagicRevealEffect();

  /**
   * @brief Starts the magic reveal animation on the target view.
   *
   * Creates a mask overlay (diagonal sweep) and a glass light band overlay,
   * then animates them across the target while micro-scaling the target.
   * The overlay creation is deferred by a short timer so the target's layout
   * has completed and its size is known.
   *
   * The overlay container is added as a child of the target, so it renders
   * on top of the target's own visual.
   *
   * @param target        The view/actor to reveal.
   * @param durationSec   Animation duration in seconds (default 1.0).
   * @param glassOpacity  Opacity of the glass light band (default 0.5).
   * @param initialScale  Starting scale of the target (default 0.98).
   */
  void Start(Dali::Actor target,
             float durationSec = DEFAULT_DURATION,
             float glassOpacity = DEFAULT_GLASS_OPACITY,
             float initialScale = DEFAULT_INITIAL_SCALE);

  /**
   * @brief Stops the reveal animation and removes all overlays.
   */
  void Stop();

  /**
   * @brief Returns true from Start() until the reveal finishes or is stopped
   *        (covers the deferred overlay build as well as the animation).
   */
  bool IsActive() const { return static_cast<bool>(mTarget); }

 private:
  /// Called by the deferred timer to build the overlay once layout is done.
  bool OnBuildTimer();

  /// Builds the mask overlay, glass band overlay, and all animations.
  void BuildOverlay();

  /// Called when the reveal animation finishes — cleans up overlays.
  void OnAnimationFinish(Dali::Animation animation);

  Dali::Actor mTarget;  ///< The target view being revealed
  Dali::Ui::View
      mContainer;  ///< Clipping container (clips overlays to target bounds)

  Dali::Ui::View mMaskOverlay;   ///< Diagonal sweep mask (placeholder color)
  Dali::Ui::View mGlassOverlay;  ///< Multi-color glass light band
  Dali::Animation mAnimation;    ///< The reveal + scale animation
  Dali::Timer mBuildTimer;  ///< Defers overlay creation until layout is done

  float mDurationSec = DEFAULT_DURATION;
  float mGlassOpacity = DEFAULT_GLASS_OPACITY;
  float mInitialScale = DEFAULT_INITIAL_SCALE;
};

}  // namespace A2ui