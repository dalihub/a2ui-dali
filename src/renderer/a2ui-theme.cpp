/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 */

#include "renderer/a2ui-theme.h"

#include <dali-ui-foundation/public-api/configuration/ui-color-manager.h>
#include <dali/integration-api/debug.h>
#include <unordered_map>
#include <cstdlib>
#include <cstring>

using namespace Dali;
using namespace Dali::Ui;

namespace A2ui
{
namespace
{
// Bundled OneUI light palette — values copied verbatim from OneUI's oneui_color.json.
// This is a stopgap until the platform supplies an OneUI ThemeLoader to dali-ui; it is
// NOT a redefinition of OneUI's tokens, just a local copy of the published values.
const std::unordered_map<std::string, uint32_t>& BundledOneUILight()
{
  static const std::unordered_map<std::string, uint32_t> kPalette = {
    {"Primary", 0x387AFF},
    {"PrimaryHigh", 0x2E65D4},
    {"Background", 0xF1F1F3},
    {"BackgroundApp", 0xFFFFFF},
    {"Surface", 0xFFFFFF},
    {"SurfaceContainerLow", 0xF1F1F3},
    {"SurfaceContainer", 0xE4E4E7},
    {"SurfaceContainerHigh", 0xD6D6D8},
    {"SurfaceContainerSemi", 0xE4E4E7},
    {"OnSurfaceContainerHighest", 0x010102},
    {"OnSurfaceContainer", 0x4D4D52},
    {"OnSurfaceContainerLow", 0x636368},
    {"OnSurfaceContainerFixed", 0xFCFCFF},
    {"Outline", 0xB7B7BB},
    {"OutlineLow", 0xD6D6D8},
    {"Red", 0xF64141},
    {"Green", 0x0BB075},
  };
  return kPalette;
}

// Live-composer (a2ui-composer.ag-ui.com/gallery) web-parity palette. Values sampled
// directly from the live gallery's high-res render. The token NAMES are unchanged
// (dali-ui/OneUI colour ids) — only the resolved values mirror the composer's web look:
// monochrome cards (white surface, near-black text, grey labels, light outlines).
// Selected by default; export A2UI_THEME=oneui to fall back to the OneUI palette above.
const std::unordered_map<std::string, uint32_t>& BundledComposerWeb()
{
  static const std::unordered_map<std::string, uint32_t> kPalette = {
    {"Primary", 0x0A0A0A},                  // composer is monochrome: the "accent" is near-black
    {"PrimaryHigh", 0x000000},
    {"Background", 0xDEDEE9},                // lavender page
    {"BackgroundApp", 0xF8F8FB},             // gallery section wrapper
    {"Surface", 0xFFFFFF},                   // white card
    {"SurfaceContainerLow", 0xF8F8FB},
    {"SurfaceContainer", 0xF1F1F3},
    {"SurfaceContainerHigh", 0xE5E5E5},
    {"SurfaceContainerSemi", 0xF1F1F3},
    {"OnSurfaceContainerHighest", 0x0A0A0A}, // near-black heading/value text
    {"OnSurfaceContainer", 0x737373},        // grey label / muted text
    {"OnSurfaceContainerLow", 0x999999},
    {"OnSurfaceContainerFixed", 0xFFFFFF},   // on-primary text
    {"Outline", 0xE5E5E5},                   // card / button / input outline (light)
    {"OutlineLow", 0xE5E5E5},                // subtle card edge
    {"Red", 0xF64141},
    {"Green", 0x0BB075},
  };
  return kPalette;
}

// OneUI dark palette — values copied from OneUI's official oneui_color.json (dark theme).
const std::unordered_map<std::string, uint32_t>& BundledOneUIDark()
{
  static const std::unordered_map<std::string, uint32_t> kPalette = {
    {"Primary", 0x387AFF},
    {"PrimaryHigh", 0x2E65D4},
    {"Background", 0x010102},
    {"BackgroundApp", 0x131315},
    {"Surface", 0x1C1C1E},
    {"SurfaceContainerLow", 0x252528},
    {"SurfaceContainer", 0x3A3A3D},
    {"SurfaceContainerHigh", 0x4D4D52},
    {"SurfaceContainerSemi", 0x3A3A3D},
    {"OnSurfaceContainerHighest", 0xFFFFFF},
    {"OnSurfaceContainer", 0xD6D6D8},
    {"OnSurfaceContainerLow", 0xB7B7BB},
    {"OnSurfaceContainerFixed", 0xFCFCFF},
    {"Outline", 0x636368},
    {"OutlineLow", 0x434348},
    {"Red", 0xF64141},
    {"Green", 0x0BB075},
  };
  return kPalette;
}

// Default = OneUI dark theme. Set A2UI_THEME=oneui to fall back to OneUI light,
// or A2UI_THEME=composer for the composer web palette.
bool UseDarkPalette()
{
  const char* t = std::getenv("A2UI_THEME");
  return !(t && (std::strcmp(t, "oneui") == 0 || std::strcmp(t, "composer") == 0));
}
} // namespace

UiColor A2uiTheme::Color(const std::string& id)
{
  // Default = OneUI dark theme. A2UI_THEME=oneui → light, A2UI_THEME=composer → composer web.
  // Call sites are unchanged — they still ask by semantic colour id.
  const char* t = std::getenv("A2UI_THEME");
  const auto& palette = UseDarkPalette() ? BundledOneUIDark()
                      : (t && std::strcmp(t, "composer") == 0) ? BundledComposerWeb()
                                                               : BundledOneUILight();
  auto it = palette.find(id);
  if(it != palette.end())
  {
    return UiColor(it->second);
  }

  Vector4 v;
  if(UiColorManager::Get().GetColor(id.c_str(), v))
  {
    return UiColor(v);
  }

  DALI_LOG_ERROR("[A2UI][theme] unknown colour id '%s'\n", id.c_str());
  return UiColor(0xFF00FF); // magenta = missing token (loud, so it is noticed)
}

} // namespace A2ui
