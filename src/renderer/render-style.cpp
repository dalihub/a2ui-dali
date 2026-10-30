/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 */

#include "renderer/render-style.h"
#include "renderer/a2ui-theme.h"

namespace A2ui
{
using Dali::Ui::UiColor;

// Colours resolve to OneUI semantic tokens via A2uiTheme (see a2ui-theme.h). Names are
// dali-ui/OneUI colour ids; values come from the bundled OneUI palette until dali-ui
// supplies a platform theme. A few (scrim, debug box) are intentional literals, not tokens.
extern const UiColor COLOR_TEXT_DEFAULT    = A2uiTheme::Color("OnSurfaceContainerHighest"); // #010102 primary text
extern const UiColor COLOR_PRIMARY         = A2uiTheme::Color("Primary");                   // #387AFF
extern const UiColor COLOR_ON_PRIMARY      = A2uiTheme::Color("OnSurfaceContainerFixed");   // #FCFCFF on-primary text
extern const UiColor COLOR_TEXT_MUTED      = A2uiTheme::Color("OnSurfaceContainer");        // #4D4D52 secondary text
extern const UiColor COLOR_CARD_BG         = A2uiTheme::Color("Surface");                   // #FFFFFF white card surface (web look)
extern const UiColor COLOR_BTN_PRIMARY     = A2uiTheme::Color("Primary");
extern const UiColor COLOR_BTN_SECONDARY   = A2uiTheme::Color("Surface");                   // #FFFFFF
extern const UiColor COLOR_BTN_BORDER      = A2uiTheme::Color("Outline");                   // #B7B7BB
extern const UiColor COLOR_DIVIDER         = A2uiTheme::Color("Outline");                   // OneUI Divider = Outline
extern const UiColor COLOR_IMG_PLACEHOLDER = A2uiTheme::Color("SurfaceContainer");          // #E4E4E7
extern const UiColor COLOR_PLACEHOLDER_BG(0xfff0f0);                                        // missing-component debug box
extern const UiColor COLOR_PLACEHOLDER_TEXT = A2uiTheme::Color("Red");                      // #F64141
extern const UiColor COLOR_ERROR           = A2uiTheme::Color("Red");
extern const UiColor COLOR_INPUT_BG        = A2uiTheme::Color("Surface");                   // #FFFFFF
extern const UiColor COLOR_INPUT_BORDER    = A2uiTheme::Color("Outline");                   // #B7B7BB input outline
// Composer is monochrome: checked boxes, slider fill/thumb are dark/grey, not the blue
// Primary. (In the OneUI fallback these read as near-black too, which is acceptable.)
extern const UiColor COLOR_CHECK_ON        = A2uiTheme::Color("OnSurfaceContainerHighest"); // dark check
extern const UiColor COLOR_CHECK_OFF       = A2uiTheme::Color("Outline");
extern const UiColor COLOR_TAB_ACTIVE      = A2uiTheme::Color("OnSurfaceContainerHighest");
extern const UiColor COLOR_TAB_INACTIVE    = A2uiTheme::Color("SurfaceContainerLow");
extern const UiColor COLOR_MODAL_OVERLAY(0x00000066);                                       // scrim (not a OneUI token)
extern const UiColor COLOR_SLIDER_TRACK    = A2uiTheme::Color("OutlineLow");                // light grey track
extern const UiColor COLOR_SLIDER_FILL     = A2uiTheme::Color("OnSurfaceContainer");        // subtle grey fill
extern const UiColor COLOR_SLIDER_THUMB    = A2uiTheme::Color("OnSurfaceContainerHighest"); // dark thumb

// Video / audio play-glyph colour (white icon on coloured background)
extern const UiColor COLOR_ICON_ON_PRIMARY  = UiColor(0xFFFFFF);
// Video frame placeholder background
extern const UiColor COLOR_VIDEO_FRAME      = UiColor(0x202024);

} // namespace A2ui
