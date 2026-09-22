#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 */

#include <dali-ui-foundation/dali-ui-foundation.h>

namespace A2ui
{
// Shared OneUI semantic colours used across the component renderers. Defined once in
// render-style.cpp (resolved via A2uiTheme); a few (scrim, debug box) are intentional
// literals, not tokens. Declared extern so each component translation unit shares them.
extern const Dali::Ui::UiColor COLOR_TEXT_DEFAULT;      // #010102 primary text
extern const Dali::Ui::UiColor COLOR_PRIMARY;           // #387AFF
extern const Dali::Ui::UiColor COLOR_ON_PRIMARY;        // #FCFCFF on-primary text
extern const Dali::Ui::UiColor COLOR_TEXT_MUTED;        // #4D4D52 secondary text
extern const Dali::Ui::UiColor COLOR_CARD_BG;           // #FFFFFF white card surface
extern const Dali::Ui::UiColor COLOR_BTN_PRIMARY;
extern const Dali::Ui::UiColor COLOR_BTN_SECONDARY;
extern const Dali::Ui::UiColor COLOR_BTN_BORDER;
extern const Dali::Ui::UiColor COLOR_DIVIDER;
extern const Dali::Ui::UiColor COLOR_IMG_PLACEHOLDER;
extern const Dali::Ui::UiColor COLOR_PLACEHOLDER_BG;    // missing-component debug box
extern const Dali::Ui::UiColor COLOR_PLACEHOLDER_TEXT;
extern const Dali::Ui::UiColor COLOR_ERROR;
extern const Dali::Ui::UiColor COLOR_INPUT_BG;
extern const Dali::Ui::UiColor COLOR_INPUT_BORDER;
extern const Dali::Ui::UiColor COLOR_CHECK_ON;
extern const Dali::Ui::UiColor COLOR_CHECK_OFF;
extern const Dali::Ui::UiColor COLOR_TAB_ACTIVE;
extern const Dali::Ui::UiColor COLOR_TAB_INACTIVE;
extern const Dali::Ui::UiColor COLOR_MODAL_OVERLAY;     // scrim (not a OneUI token)
extern const Dali::Ui::UiColor COLOR_SLIDER_TRACK;
extern const Dali::Ui::UiColor COLOR_SLIDER_FILL;
extern const Dali::Ui::UiColor COLOR_SLIDER_THUMB;

// Video / audio play-glyph colour (white icon on coloured background)
extern const Dali::Ui::UiColor COLOR_ICON_ON_PRIMARY;   // 0xFFFFFF
// Video frame placeholder background (dark)
extern const Dali::Ui::UiColor COLOR_VIDEO_FRAME;        // 0x202024

} // namespace A2ui
