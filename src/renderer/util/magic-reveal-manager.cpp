/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * MagicRevealManager implementation — see header for design overview.
 */

#include "renderer/util/magic-reveal-manager.h"

#include <algorithm>

#include <dali-ui-foundation/public-api/visuals/visual-properties.h>
#include <dali/integration-api/debug.h>

namespace A2ui {

MagicRevealManager::~MagicRevealManager() {
  Clear();
}

void MagicRevealManager::Register(Dali::Ui::ImageView imageView,
                                  float delaySec) {
  if (!imageView) {
    return;
  }

  Prune();

  // Avoid duplicate registration
  for (auto& e : mEntries) {
    if (e.imageView == imageView) {
      return;
    }
  }

  Entry entry;
  entry.imageView = imageView;
  entry.delaySec = delaySec > 0.0f ? delaySec : 0.0f;
  entry.revealed = false;

  // Connect to ResourceReadySignal — fires when the image finishes loading.
  imageView.ResourceReadySignal().Connect(this,
                                          &MagicRevealManager::OnResourceReady);

  // If the image is already loaded, trigger immediately.
  auto status = imageView.GetLoadingStatus();
  if (status == Dali::Ui::Visual::ResourceStatus::READY) {
    size_t idx = mEntries.size();
    mEntries.push_back(std::move(entry));
    StartReveal(idx);
  } else {
    mEntries.push_back(std::move(entry));
  }
}

void MagicRevealManager::Unregister(Dali::Ui::ImageView imageView) {
  for (auto it = mEntries.begin(); it != mEntries.end(); ++it) {
    if (it->imageView == imageView) {
      if (it->delayTimer) {
        it->delayTimer.Stop();
        it->delayTimer.Reset();
      }
      if (it->effect) {
        it->effect->Stop();
        it->effect.reset();
      }
      mEntries.erase(it);
      return;
    }
  }
}

void MagicRevealManager::Prune() {
  // Entries are addressed by index from pending delay timers, so only compact
  // the list when none is pending.
  for (const auto& e : mEntries) {
    if (e.delayTimer) {
      return;
    }
  }
  // Drop entries with nothing left to do: the reveal has run, or the image view
  // is referenced by this list alone (its view tree is gone). Without this every
  // image the renderer ever built stays alive for the renderer's lifetime.
  mEntries.erase(
      std::remove_if(mEntries.begin(), mEntries.end(),
                     [](const Entry& e) {
                       if (e.effect && e.effect->IsActive()) {
                         return false;
                       }
                       return e.revealed ||
                              e.imageView.GetBaseObject().ReferenceCount() == 1;
                     }),
      mEntries.end());
}

void MagicRevealManager::Clear() {
  for (auto& e : mEntries) {
    if (e.delayTimer) {
      e.delayTimer.Stop();
      e.delayTimer.Reset();
    }
    if (e.effect) {
      e.effect->Stop();
      e.effect.reset();
    }
  }
  mEntries.clear();
}

void MagicRevealManager::OnResourceReady(Dali::Ui::View view) {
  // Find the entry whose ImageView matches the signalled View.
  for (size_t i = 0; i < mEntries.size(); ++i) {
    if (mEntries[i].imageView == view) {
      if (!mEntries[i].revealed) {
        StartReveal(i);
      }
      return;
    }
  }
}

void MagicRevealManager::StartReveal(size_t index) {
  if (index >= mEntries.size()) {
    return;
  }

  Entry& e = mEntries[index];
  if (e.revealed) {
    return;
  }
  e.revealed = true;

  if (e.delaySec > 0.0f) {
    // Stagger: start a timer, then fire the reveal when it elapses.
    e.delayTimer = Dali::Timer::New(static_cast<int>(e.delaySec * 1000.0f));
    e.delayTimer.TickSignal().Connect(
        this, [this, index]() -> bool { return OnDelayTimer(index); });
    e.delayTimer.Start();
  } else {
    // No delay — start immediately.
    e.effect = std::make_shared<MagicRevealEffect>();
    e.effect->Start(e.imageView, e.durationSec, e.glassOpacity, e.initialScale);
  }
}

bool MagicRevealManager::OnDelayTimer(size_t index) {
  if (index >= mEntries.size()) {
    return false;
  }

  Entry& e = mEntries[index];
  if (e.imageView && !e.effect) {
    e.effect = std::make_shared<MagicRevealEffect>();
    e.effect->Start(e.imageView, e.durationSec, e.glassOpacity, e.initialScale);
  }

  // One-shot timer
  if (e.delayTimer) {
    e.delayTimer.Stop();
    e.delayTimer.Reset();
  }
  return false;
}

}  // namespace A2ui