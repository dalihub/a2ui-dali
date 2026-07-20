#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 *
 * Internal implementation header shared by the per-component renderer files
 * (renderer/components/*.cpp). It bundles the DALi + renderer includes and the usings
 * each A2uiRenderer::Render* definition needs, so each component file stays small.
 * Internal only — do not include from public headers.
 */

#include "renderer/a2ui-renderer.h"
#include "renderer/render-style.h"
#include "renderer/a2ui-theme.h"
#include "renderer/a2ui-metrics.h"
#include "renderer/a11y-bridge.h"

#include <dali/integration-api/debug.h>
#include <dali/public-api/events/tap-gesture-detector.h>
#include <dali/public-api/events/touch-event.h>
#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali-ui-foundation/public-api/views/image/image-view.h>
#include <dali-ui-foundation/public-api/layouts/flex-layout-params.h>
#include <dali-ui-foundation/public-api/views/scroll/scroll-view.h>
#include <dali-ui-foundation/public-api/text/text-enumerations.h>
#include <dali-ui-foundation/public-api/text/style/underline.h>

#include <cstring>
#include <cstdlib>
#include <sstream>
#include <fstream>

using namespace Dali;       // intentional in this internal-only implementation header
using namespace Dali::Ui;
using Dali::Ui::Integration::TreeNode;

namespace A2ui
{
/**
 * @brief Applies inter-item spacing (gap) to a flex child by setting its leading margin on the
 * main axis, while PRESERVING the item's own template margins on the cross axis.
 *
 * DALi's FlexLayout has no CSS-style `gap`/`spacing` property, so a list fakes the gap with
 * per-item margins. The previous code set the margin outright — `SetMargin(Extents(g,0,0,0))` for
 * a ROW — which also zeroed the item's cross-axis (top/bottom) margin. The first item (isFirst)
 * never gets the gap, so it kept its template cross margin while later items lost theirs; under
 * align-items:stretch that cross-margin mismatch stretched the first item to a different height —
 * issue #12 ("first item shorter"). Preserving the cross-axis margin keeps every item's cross
 * extents identical, so they all stretch to the same size.
 *
 * The main-axis behaviour is unchanged from before (leading = gap, trailing = 0), so vertical
 * lists — whose cross axis (start/end) is normally zero anyway — render exactly as they did.
 *
 * @param[in] item        The child view to space (no-op if empty).
 * @param[in] horizontal  true for a ROW list (gap on the start edge), false for a COLUMN (top edge).
 * @param[in] gap         Logical gap already dp-scaled; no-op if <= 0.
 * @param[in] isFirst     The leading item receives no gap and is left untouched.
 */
inline void ApplyItemGap(View item, bool horizontal, float gap, bool isFirst)
{
  if(!item || isFirst || gap <= 0.0f) return;
  const int16_t g = static_cast<int16_t>(gap);
  const Extents m = item.GetMargin();
  if(horizontal)
    item.SetMargin(Extents(g, 0, m.top, m.bottom));  // main = start/end; cross = top/bottom preserved
  else
    item.SetMargin(Extents(m.start, m.end, g, 0));    // main = top/bottom; cross = start/end preserved
}
} // namespace A2ui
