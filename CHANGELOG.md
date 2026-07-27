# Changelog

All notable changes to **a2ui-dali** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.16.0] — 2026-07-27

Adds the A2UI **v1.0 candidate** features the renderer was missing, and fixes a deletion
bug the new tests exposed. Everything added here is additive: v0.9 payloads take the same
paths they did before, and the 29-screen gallery corpus is pixel-identical to 0.15.1.

v1.0 is a release candidate — the upstream reference web renderers still ship only v0_8 and
v0_9 — so nothing here switches the renderer over to it. Each feature is accepted alongside
its v0.9 spelling, which continues to work unchanged; this release makes a v1.0 payload
render, it does not make v0.9 stop rendering. No a2ui-dali C++ API changes.

### Fixed

- **Deleting the last key reported a failure.** The shared DALi JSON parser rejects an
  empty object, so emptying the model routed through `SetData("/", "{}")` and came back
  false — `updateDataModel` answered "could not remove" and the host saw the line fail,
  even though the key had in fact been removed. An empty model is now held as a
  parser-less empty document, which is what reads and the next write already assumed.
  `DeleteAtPath("/")` (clear everything) was affected the same way.

### Added

- **`@index` built-in.** Returns the 0-based iteration index inside a list template, with an
  optional `offset` argument for 1-based numbering. The `@` prefix is reserved for system
  context evaluations, so it resolves from the render context rather than the catalog, and
  `args` is optional. A2UI restricts it to Collection Scope: outside a list template it is
  an evaluation error, not a `0`. `DataContext::CreateCollectionItemContext()` is what marks
  a context as an iteration, so only real template items can answer it.
- **`createSurface` accepts `surfaceProperties`.** v1.0's name for `theme`; the v0.9
  spelling keeps working, with `surfaceProperties` winning if both are sent.
- **`createSurface` accepts inline `components` and `dataModel`,** so a complete first frame
  can arrive in one message instead of create + updateDataModel + updateComponents. Data is
  applied before components, so the tree renders against a populated model exactly as it
  would have from the three-message sequence.
- **`Video.posterUrl`.** The still shown before playback is drawn as the media frame's
  background, so the play glyph stays on top of the artwork instead of a flat dark box.
- **`Slider.steps`.** Divides the range into discrete intervals and snaps the value to the
  nearest stop.
- **`a2uiRendererCapabilities` / `a2uiRendererDataModel` A2A metadata.** v1.0 renamed these
  keys (client → renderer). Both spellings are now sent with identical values, so an agent
  on either version finds the key it looks for.

### Compatibility

- No change to the rendered output, the component catalog, or the v0.9 wire format. Still
  builds against `dali-ui v2.5.30.10913` with `dali2-core`/`dali2-adaptor` `dali_2.5.31`.
- Verification: conformance **135/135** (16 new checks covering `@index` scoping and its
  offset, `surfaceProperties`, an inline `createSurface` payload, and both deletion
  spellings), streaming render **82/82**, and the 29-screen gallery corpus **29/29
  pixel-identical** to 0.15.1.

## [0.15.1] — 2026-07-27

Corrects the 0.15.0 release, which shipped a test suite it could not pass.

### Fixed

- Removed four conformance tests that entered the 0.15.0 commit by mistake. They cover
  `createSurface.surfaceProperties`, an inline `createSurface` payload, and the `@index`
  built-in — v1.0 features whose implementation is not part of this release — so 0.15.0
  reported 128/136. One of them also asserted that an `updateDataModel` with an omitted
  `value` is rejected, which contradicts the v0.9.1 deletion rule this release implements.
  The suite is 119/119 again. No source change: 0.15.0's renderer and library code were
  correct and are unchanged here.

## [0.15.0] — 2026-07-27

Closes the gaps found by auditing the renderer against the upstream A2UI module blueprints
(`blueprints/modules/a2ui_core.blueprint.md`). Six data-layer rules were not implemented
the way the specification and the reference `web_core` implementation define them; the most
consequential is that **a data-model key could not be deleted by any means**. One change is
a behaviour break for embedders — a duplicate `createSurface` is now rejected — which is
why this is a minor bump. Rendering is untouched: the 29-screen gallery corpus is
pixel-identical to 0.14.0.

### Fixed

- **`updateDataModel` can delete again.** v0.9.1 spells a deletion as an omitted `value`
  ("If omitted, the key at `path` is removed"); the message was instead **rejected** as
  malformed. v1.0 spells the same intent as an explicit `value: null`; that was **stored as
  a literal null**. Both forms now remove the key, so an agent dropping an item from a list
  or clearing a field actually clears it. An array slot is emptied rather than spliced out,
  keeping the array's length so later indices — and the components bound to them — do not
  shift. New `DataModel::DeleteAtPath()` is the underlying primitive.
- **A numeric path segment auto-creates an Array, not an Object.** Writing `/items/0/name`
  into a model with no `/items` produced `{"items":{"0":{…}}}`, which every data-driven
  list bound to `/items` then failed to read. It now produces `{"items":[{…}]}`, matching
  the JSON Pointer auto-typing rule and `web_core`. An index past the end pads the array
  with empty slots instead of shifting the value down.
- **A Button's `checks` now disable it.** The specification is explicit: "Buttons can also
  define `checks`. If any check fails, the button is automatically disabled." `checks` were
  only ever wired to `TextField` and `DateTimeInput` for their inline error text, so a
  submit button stayed live and fired its action while the form was invalid. A Button (and
  a clickable Card) with failing rules is now rendered disabled and neither tap nor the
  remote's OK key dispatches; it re-enables reactively as soon as the data satisfies the
  rules. Where a Card borrows its action from a descendant, the gate follows the component
  that declares the rules, so a card tap cannot bypass a disabled Button inside it.
- **`\${` renders as a literal `${`.** The escape defined by the basic catalog was not
  implemented: `formatString` consumed the sequence and dropped the rest of the token, so
  `cost \${5}` printed as `cost \`. Escaped tokens are now left as text — and are no longer
  registered as data-model dependencies.
- **Type coercion follows the standard.** `GetBool` matched `"true"`/`"false"`
  case-sensitively and ignored numbers entirely, returning the caller's fallback instead.
  `"TRUE"` is now true, any other string is false (previously an arbitrary string could
  read as true when the caller passed `fallback=true`), a non-zero number is true and zero
  is false.

### Added

- **`a2uiClientDataModel` is actually sent.** `sendDataModel: true` was parsed and carried
  as far as the action dispatcher but never used, so an agent that asked for the surface's
  state received nothing. `SurfaceGroupModel::GetClientDataModel()` returns the
  `{"version":…,"surfaces":{…}}` payload for every surface with the flag set, and the A2A
  example transport attaches it to outgoing message metadata via the new
  `A2aTransportAdapter::SetClientDataModelProvider()`. The provider is called on the caller's
  thread and snapshotted before the request reaches the worker, so it never reads the data
  model while the UI is writing to it.
- `A2uiHost::Reset()` / `A2uiMessageProcessor::Reset()` — start a fresh A2UI session. A host
  that replays a stream it has already shown (a screen browser stepping back to an earlier
  example) needs this, since surface ids are unique per session.

### Changed

- **A duplicate `createSurface` is now an error.** A `surfaceId` is unique per client
  session; re-creating one that is still active used to silently recreate the surface,
  hiding a stream that had lost a `deleteSurface`. The processor now rejects it and reports
  it through `GetLastError()`. The id is free again after `deleteSurface`. **Embedders that
  re-feed a previously shown stream must call `A2uiHost::Reset()` first** — the bundled
  gallery demo does this when switching screens.

### Compatibility

- No change to the on-the-wire format, the component catalog, or the rendered output. Still
  builds against `dali-ui v2.5.30.10913` with `dali2-core`/`dali2-adaptor` `dali_2.5.31`.
- Verification: conformance **119/119** (up from 99 — 20 new checks covering auto-typing,
  deletion, surface-id uniqueness, coercion, escaping and data-model sync), streaming render
  **82/82**, and the 29-screen gallery corpus **29/29 pixel-identical** to 0.14.0.

## [0.13.0] — 2026-07-22

Automated release tracking **dali-ui v2.5.30.10913** (with `dali2-core` / `dali2-adaptor` `dali_2.5.31`).

### Changed

- Switched child traversal in the image binding, tabs content swap, and `ViewPool::Release` from `GetChildCount()`/`GetChildAt()` to the View-typed `GetChildViewCount()`/`GetChildViewAt()` accessors introduced in dali-ui v2.5.30.
- A Row that explicitly declares `align: start`, `end`, or `center` now propagates that cross-axis alignment to width-pinned children: such children are laid out with `FlexAlign::AUTO` so they inherit the Row's `alignItems`, instead of being forced to `FlexAlign::CENTER`.
- Rows that declare no `align` keep the previous implicit `FlexAlign::CENTER` behaviour for width-pinned children, so icon/thumbnail + text lines are unchanged.
- Added an explicit `<memory>` include to `a2ui-renderer.h`, which the dali-ui umbrella header no longer pulls in transitively.

### Compatibility

- Built against `dali-ui v2.5.30.10913` with `dali2-core`/`dali2-adaptor` `dali_2.5.31` on the
  desktop `dali-env` build. Gallery corpus verified against the previous release
  (pixel-regression gate + visual judge). Conformance: 82/82.

## [0.14.0] — 2026-07-27

Makes data binding actually reactive and brings the renderer-to-agent wire format in line
with the A2UI specification. **The action message changed shape** — see *Fixed* — so agents
reading the old `userAction` key must be updated; that break is why this is a minor bump
rather than a patch. No a2ui-dali C++ API changes.

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
  Writes now address an existing index or the JSON Pointer append slot (`-`, or
  `index == length`); anything else is rejected and logged instead of reshaping the list.
  A root-level array is handled too, and `ResolvePath` no longer reports an out-of-range
  index as a hit (`/f/2` on a two-element list used to resolve to the array itself).

- **Bound `Icon.name` follows the data.** It was resolved once, which already broke three
  shipped payloads under a streaming feed: the music player lost its pause glyph, the stats
  card both its trend icons, the shipping card its "out for delivery" truck.
- **A bound `AudioPlayer.description` can appear at all.** An empty caption at first paint
  returned the bare control, leaving no label for the value to arrive into.
- **Validation rules that read another field re-evaluate.** `checks` watched only the
  field's own value, so `required(/other)` kept its error on screen after `/other` was
  filled in.
- **Tap targets no longer accumulate across list rebuilds.** Each rebuild attached fresh
  detectors while the old ones were kept for the life of the surface, holding views that
  are no longer on screen.
- **`ClearObservers()` called from inside a notification is deferred** to the end of the
  pass instead of destroying the callback that is running and skipping the observers after
  it.
- **Renderer-to-agent `action` messages now match the spec.** The outgoing envelope used
  the v0.8 message key `userAction` and omitted three fields the v0.9/v0.9.1 schema marks
  required, so spec-conformant agents could not parse it. It is now
  `{"version":"v0.9","action":{…}}` carrying `name`, `surfaceId`, `sourceComponentId`, an
  ISO 8601 `timestamp`, and `context` (emitted as `{}` when the component declares none).
  Agents that only read the old `userAction` key must be updated.
- **A2UI media type follows the IANA convention.** Payloads are now labelled
  `application/a2ui+json` instead of `application/json+a2ui`. Incoming DataParts accept
  either spelling, so agents that have not migrated keep working.
- **`callFunction` reads `functionCallId` and `wantResponse` from the envelope.** Both were
  being looked up inside the `callFunction` body, where they never appear, so the call ID
  was always empty and a requested `functionResponse` was never sent. Only `call` and
  `args` live in the body.

### Added

- `src/core/a2ui-protocol.h` — wire-level constants (`A2UI_PROTOCOL_VERSION`,
  `A2UI_MIME_TYPE`, `A2UI_MIME_TYPE_LEGACY`, `IsA2uiMimeType`) shared by the message layer
  and the transports.
- `a2ui-streaming-render-test` — an end-to-end test that drives the real renderer and
  asserts on the real DALi view tree. Every case is fed message-by-message, as one string,
  and as a batched file, and all three must agree; every shipped sample and gallery screen
  is additionally rendered both ways and compared. `tools/run-tests.sh` runs it together
  with the conformance test.
- Core unit tests for the data model's array-write rules and observer bookkeeping, in
  `a2ui-conformance-test` (no display needed).
- `DataModel::UnwatchAll()` / `ObserverCount()` — retire exactly the watches a rebuilt
  subtree registered, so repeated rebuilds cannot accumulate observers on dead views, and
  make the count observable so a test can prove it. A nested list is covered: each
  generation records its watches into every enclosing generation, so an outer rebuild also
  retires what an inner rebuild registered after it.

### Compatibility

- Built against `dali-ui v2.5.30.10913` with `dali2-core`/`dali2-adaptor` `dali_2.5.31` on
  the desktop `dali-env` build — unchanged from 0.13.0. Conformance: 99/99; streaming
  render: 82/82. The 29-screen gallery corpus renders pixel-identical to 0.13.0
  (mean-abs-diff 0.000 on every screen).

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
