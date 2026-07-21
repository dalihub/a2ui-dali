# Changelog

All notable changes to **a2ui-dali** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Reactive data binding for FunctionCall properties.** A property rendered from a
  `{"call": …}` binding never followed the data model. Where the arguments held a nested
  `{"path": …}` the update wrote the **raw** value over the formatted one (`formatDate`
  showed `2025-12-16` instead of `Tue`, `formatCurrency` `12458.32` instead of
  `$12,458.32`); where the only inputs were `${…}` tokens inside a `formatString` template
  no watch was registered at all and the property stayed frozen. Bindings now collect every
  path they read — direct `path`, nested paths in `args`, and each `${…}` token — and
  **re-evaluate the whole binding** when any of them changes, matching the first paint.
  ([#14](https://github.com/dalihub/a2ui-dali/issues/14))
- **Data-driven child lists now follow their array.** `children: {path, componentId}` was
  filled once at render time, so a list whose array arrived in a later `updateDataModel`
  (the weather card's forecast row) stayed permanently empty. The bound array is now
  watched and the children rebuilt when its length changes; per-item value changes continue
  to go through each item's own bindings, so rows are not thrown away on every update.
- **`updateDataModel` at an array-item path no longer destroys the array.** A path like
  `/forecast/1/temp` rewrote the parent list as an object (`{"0":…,"1":…}`), which read back
  fine but left any data-driven list bound to it rendering nothing on the next render.

### Added

- `a2ui-streaming-render-test` — an end-to-end test that drives the real renderer and
  asserts on the real DALi view tree. Every case is fed message-by-message, as one string,
  and as a batched file, and all three must agree; every shipped sample and gallery screen
  is additionally rendered both ways and compared. `tools/run-tests.sh` runs it together
  with the conformance test.
- `DataModel::NextObserverId()` / `UnwatchRange()` — retire exactly the watches a rebuilt
  subtree registered, so repeated rebuilds cannot accumulate observers on dead views.

## [0.12.0] — 2026-07-16

Adds TV remote / D-pad focus support and folds in three rendering fixes. No a2ui-dali
public API changes; still builds against **DALi UI 2.5.28** (`dali2-core` / `dali2-adaptor`
2.5.29).

### Added

- **TV remote / D-pad focus.** Interactive views — Button, clickable Card, Tabs (tab
  buttons), and Modal open/close controls — are now keyboard-focusable, so DALi's
  `FocusManager` moves focus between them and draws the focus highlight, and the remote
  **OK/Enter** key runs the same action as a tap. Touch stays on each component's existing
  `TapGestureDetector`; a single `EnableKeyActivation` helper wires the key path beside it so
  the tap and key handlers cannot diverge and no interactive component is missed. The example
  hosts set the initial focus (`FocusManager::RequestFocus`) after a surface renders.
  Verified on a Tizen 11 emulator: focus navigation on both axes, OK-key activation, and
  switching Tabs by remote.

### Fixed

- **Label height collapsed or hid data-bound text.** Label height is now derived from the
  real line count, so text that arrives after the initial layout is no longer collapsed or
  clipped.
- **`pause.png` / `playPause.png` duplicated `play.png`.** The media-player pause and
  play/pause icons shipped as copies of the play glyph; they now carry the correct artwork
  (issue #5).
- **Row image/text overlap at narrow widths.** A data-bound Row image slot is pinned so a
  sibling text no longer overlaps it when the row is narrow.

## [0.11.0] — 2026-07-07

Ports the renderer to the reorganized **DALi UI 2.5.28** API and folds in the web-composer
parity refinements developed against 0.10.0. No a2ui-dali public API changes.

### Changed

- **Build against DALi UI 2.5.28** (with `dali2-core` / `dali2-adaptor` 2.5.29). dali-ui
  2.5.28 reorganized its headers into category directories and moved the JSON builder out of
  `devel-api`, so the renderer's includes and type references are updated:
  - Builder headers `dali-ui-foundation/devel-api/builder/{tree-node,json-parser}.h` →
    `integration-api/builder/…`, and their types `Dali::Ui::{TreeNode,JsonParser}` →
    `Dali::Ui::Integration::…`.
  - View/text headers moved under `public-api/{views,types,configuration}/…`
    (`image-view`, `label`, `scroll-view`, `unit`, `ui-color-manager`).
  - `Label::SetUnderline` → `SetTextUnderline`; the fluent-chaining view setters (now
    returning `void`) and `View::AsInteractive()` (now returning the `InteractiveTrait`) are
    called in their non-chained forms.

  Rendered output is preserved — the gallery corpus renders structurally identically to
  0.10.0, with only sub-pixel text-antialiasing / image-resampling differences from the
  newer DALi text and render pipeline.
- **Closer parity with the A2UI web composer.** Refined the shared design metrics and
  per-component rendering — glyph-weighted text-width measurement, flex-container
  sizing/spacing, responsive image fitting, and button/chip/card/list details — with no
  per-card special casing.

### Fixed

- **Icons rendered at half size.** Icon size was applied as a raw logical value instead of
  the shared dp scale, so inline and header glyphs appeared as faint specks; icons now
  dp-scale like every other component and default to the muted secondary colour to match the
  web's light Material outline glyphs.

### Compatibility

- Built against **`dali-ui` `v2.5.28.10837`** with **`dali2-core` / `dali2-adaptor`
  `dali_2.5.29`** on the desktop `dali-env` build. dali-ui trails core/adaptor by one minor
  version, so pair dali-ui 2.5.28 with core/adaptor 2.5.29. a2ui-dali tracks the `dali-ui`
  API, so check out the `dali-ui` revision matching your target's DALi version. Source
  `setenv` from your `dali-env` before building. Conformance: 68/68.

## [0.10.0] — 2026-07-01

Adds a Tizen build path and brings the desktop render output into parity with the A2UI
web composer. No public API changes.

### Added

- **Tizen / gbs build.** RPM packaging under `packaging/` (`a2ui-dali.spec`,
  `a2ui-dali.manifest`) builds a2ui-dali for Tizen with
  [gbs](https://docs.tizen.org/platform/reference/gbs/gbs-overview/), alongside the
  existing desktop CMake build. The package installs the example binaries and their
  runtime resources (`res/`, gallery `screens/`, `samples/`) under
  `/usr/share/a2ui-dali`; launch the examples from that data dir so the relative `res/`
  path resolves. See the README for pointing gbs at a DALi snapshot that matches the
  target device.

### Changed

- **Rendering parity with the A2UI web composer** — web-matched design tokens (spacing,
  radii, colours, font sizes) and full-width images so cards lay out like the reference
  composer.
- `CMakeLists.txt` applies the `$DESKTOP_PREFIX` link path only when it is set, so a
  single build script serves both the desktop (`dali-env`) and the gbs/RPM builds.

### Fixed

- Number and date value formatting in the `${…}` expression evaluator.
- Card bottom padding: DALi `FlexLayout` drops the bottom inset of a `WRAP_CONTENT`
  column, leaving every card one padding short — an explicit bottom spacer restores the
  symmetric inset.

### Compatibility

- Verified on a **Tizen 11 emulator (DALi 2.5.25)** via gbs and on the desktop `dali-env`
  build. a2ui-dali tracks the `dali-ui` `devel` API, so check out the `dali-ui` revision
  that matches your target's DALi version (this release was built against `dali-ui`
  `12b0de06`, DALi 2.5.25). Source `setenv` from your `dali-env` before the desktop build.

## [0.9.0] — 2026-06-12

Initial public release of **a2ui-dali**, a native C++/[DALi](https://github.com/dalihub)
renderer for the [A2UI v0.9](https://a2ui.org) protocol. It turns an A2UI message stream
into native DALi views: you feed it messages as they arrive, and it builds and
incrementally updates the UI, raises an event with the root view for each surface, and
reports user actions back to you.

**Status — 0.9 (pre-1.0).** The v0.9 catalog is feature-complete and rendering is
regression-tested, but the public API and theming are not yet frozen and the renderer
tracks a moving `dali-ui` `devel` API (see *Compatibility*). Expect breaking changes
before 1.0.

### Highlights

- **Full A2UI v0.9 catalog** rendered onto DALi — layout, text, media, inputs, lists,
  tabs, and a modal — with two-way data binding, `${…}` expression evaluation, list
  templating, and form validation (`checks`).
- **`A2uiHost` facade** — one object owns the surface registry, message parser, and
  renderer. Multi-surface routing by `surfaceId`, per-surface `theme` / `sourceApp`, and
  host events (`OnBeginRenderingSurface` / `OnDeleteSurface` / `OnUserAction`).
- **Component-handler registry** — each component type maps to a handler; the standard
  catalog is the set registered at construction, and a **custom catalog is extra handlers
  registered onto the same renderer** via `RegisterComponent` — no renderer subclass.
- **Distributable** — installs a static library, public headers, and a `pkg-config` file.
- **Deterministic, regression-tested rendering** held to a pixel-level screenshot suite.

### Added

- **Host & integration**
  - `A2uiHost` facade with multi-surface support, per-surface `theme` (`width`, `height`,
    `pattern`), `sourceApp`, and `sendDataModel`.
  - `JsonFeed` accepts a JSON array of messages, newline-delimited JSONL, or a single
    object; `JsonFeedFile` renders a whole file as one batch (deferred render).
  - Install rules + `a2ui-dali.pc` `pkg-config` so the library is consumable from another
    build with `pkg-config --cflags --libs a2ui-dali`.
- **Catalog** — Text, Image (responsive; `avatar` → circular mask), Icon (tintable
  Material set), Divider, Row/Column (`FlexLayout`), List (templated via data path), Card,
  Tabs, Button, TextField, CheckBox, ChoicePicker, Slider, ProgressBar, DateTimeInput,
  Modal, and view-composition skeletons for Video and AudioPlayer.
- **Custom catalogs** — `A2uiRenderer::RegisterComponent(type, handler)` public API and a
  `RenderContext` that gives a custom handler the same services the built-ins use (data
  binding via `ResolveString` / `ResolveFloat` / `GetBoundPath`, the action dispatcher,
  and `RenderChild(id)` to recurse). See `examples/custom-component/`.
- **Examples** — `basic-renderer`, `gallery-demo` (keyboard catalog browser),
  `custom-component` (registers a `Badge` type), `a2a-integration`, and ready-to-run v0.9
  sample streams under `examples/samples/`.
- **Tooling** — a screenshot capture harness (`tools/capture.sh`) and a pixel regression
  runner (`tools/regress.sh`) that diffs the gallery examples against a golden baseline.

### Architecture

- The renderer dispatches each component through a **registry** (`ComponentRegistry`),
  with one file per component under `src/renderer/components/`; `a2ui-renderer.cpp` holds
  only the dispatch, entry point, and shared helpers (385 lines). Shared state is passed
  explicitly through `RenderContext`.
- A2UI catalogs are a **negotiated set of component types**, not a code artifact. The
  registry *is* the catalog: the standard catalog is the built-in handler set, and a custom
  `catalogId` needs no special-casing because the renderer renders whatever types are
  registered. This matches the official A2UI renderers (Lit / Angular / React).

### Theming

- Colours resolve through OneUI semantic tokens (`A2uiTheme`) with a bundled palette
  fallback; the standard catalog renders on a white card surface with rounded image corners
  by default, sourced from the theme rather than hard-coded per component.
- Image sizes, font sizes, radii, borders, and spacing are centralised in `A2uiMetrics` and
  expressed in density-independent `dp` units for clean high-DPI scaling.

### Verification

- **Conformance**: 68/68 parser/model assertions pass (`a2ui-conformance-test`).
- **Screenshot regression**: 29/29 gallery examples render pixel-identical to the golden
  baseline (`tools/regress.sh`); renders are deterministic, so any non-zero mean-abs-diff
  flags a regression.
- **Clean build**: builds from scratch with zero warnings/errors against the DALi revision
  below.

### Known limitations

- A trailing inline item of small text inside a flex-grow row (e.g. a duration or price at
  the far end of a row) can clip, because DALi flex-grow does not reserve width for a
  trailing sibling. Tracked for a follow-up.
- Video and AudioPlayer are view-composition skeletons (poster/art + transport affordance),
  not real media playback.

### Compatibility

- Built against `dali2-core` / `dali2-adaptor` **2.5.24** (`devel/master`) and the current
  `dali-ui` `devel` typed-visual API (`Ui::VisualType`, `Ui::ImageView`, by-value signal
  slots). `dali-ui` UI/visual APIs change frequently; track a `dali-ui` revision from the
  same period. Source `setenv` from your `dali-env` before building.

[0.11.0]: https://github.com/dalihub/a2ui-dali/releases/tag/v0.11.0
[0.10.0]: https://github.com/dalihub/a2ui-dali/releases/tag/v0.10.0
[0.9.0]: https://github.com/dalihub/a2ui-dali/releases/tag/v0.9.0
