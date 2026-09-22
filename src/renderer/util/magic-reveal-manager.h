#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * MagicRevealManager — coordinates MagicRevealEffect across multiple ImageView.
 *
 * Each registered ImageView is monitored via ResourceReadySignal(). When the
 * image finishes loading for the first time, a MagicRevealEffect is created and
 * played on that ImageView. An optional stagger delay lets you cascade the
 * reveals so they don't all fire at once.
 */

#include "renderer/util/magic-reveal-effect.h"

#include <dali-ui-foundation/public-api/views/image/image-view.h>
#include <dali/public-api/adaptor-framework/timer.h>
#include <dali/public-api/signals/connection-tracker.h>

#include <memory>
#include <vector>

namespace A2ui {

/**
 * @brief Manages MagicRevealEffect playback for multiple ImageView.
 *
 * Register() each ImageView; the manager listens for ResourceReadySignal and
 * automatically plays the reveal animation the first time the image becomes
 * ready. An optional delay (in seconds) staggers the start so multiple images
 * reveal in sequence rather than simultaneously.
 */
class MagicRevealManager : public Dali::ConnectionTracker {
 public:
  MagicRevealManager() = default;
  ~MagicRevealManager();

  /**
   * @brief Registers an ImageView for automatic magic-reveal on first display.
   *
   * Call this right after creating an ImageView. The manager listens for
   * ResourceReadySignal and automatically plays the reveal animation the
   * first time the image becomes ready. If the image is already loaded
   * (ResourceStatus::READY), the reveal starts immediately.
   *
   * @param imageView  The ImageView to reveal.
   * @param delaySec   Optional delay before the reveal starts, in seconds.
   *                   Use this to stagger multiple images (default 0).
   */
  void Register(Dali::Ui::ImageView imageView, float delaySec = 0.0f);

  /**
   * @brief Unregisters an ImageView and cancels any pending or active reveal.
   */
  void Unregister(Dali::Ui::ImageView imageView);

  /**
   * @brief Cancels all reveals and clears all registered ImageViews.
   */
  void Clear();

 private:
  /// Called when a registered ImageView's resource becomes ready.
  void OnResourceReady(Dali::Ui::View view);

  /// Starts the reveal (immediately or after the stagger delay).
  void StartReveal(size_t index);

  /// Called by the stagger-delay timer to fire the reveal.
  bool OnDelayTimer(size_t index);

  struct Entry {
    Dali::Ui::ImageView imageView;
    std::shared_ptr<MagicRevealEffect> effect;
    float durationSec = MagicRevealEffect::DEFAULT_DURATION;
    float glassOpacity = MagicRevealEffect::DEFAULT_GLASS_OPACITY;
    float initialScale = MagicRevealEffect::DEFAULT_INITIAL_SCALE;
    float delaySec = 0.0f;
    bool revealed = false;
    Dali::Timer delayTimer;
  };

  std::vector<Entry> mEntries;
};

}  // namespace A2ui