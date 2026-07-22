/* Copyright (c) 2026 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DALI_GENERATIVE_UI_A2UI_RENDERER_H
#define DALI_GENERATIVE_UI_A2UI_RENDERER_H

#include "../core/surface-model.h"
#include "../core/data-context.h"
#include "../core/expression-parser.h"
#include "../core/action-dispatcher.h"
#include "../core/diff-engine.h"
#include "view-pool.h"
#include "component-registry.h"
#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali/public-api/events/tap-gesture-detector.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace A2ui
{

/**
 * Converts SurfaceComponentsModel + DataModel into a DALi View tree.
 *
 * Supports the standard A2UI catalog:
 *   Layout : Row, Column, Card, List, Tabs, Modal, Divider
 *   Text   : Text, Icon, Image
 *   Inputs : Button, TextField, CheckBox, ChoicePicker, Slider, DateTimeInput
 */
class A2uiRenderer : public Dali::ConnectionTracker
{
public:
  A2uiRenderer();

  void SetImageDir(const std::string& imageDir) { mImageDir = imageDir; }

  /**
   * Render the surface model into a DALi View tree.
   * DataModel is non-const because input components write back to it.
   */
  Dali::Ui::View Render(SurfaceModel& surface);

  /// How many tap targets the renderer is holding alive (diagnostics — a list that rebuilds
  /// without releasing its old detectors shows up here as a count that keeps climbing).
  size_t GetTapDetectorCount() const { return mTapDetectors.size(); }

  ActionDispatcher& GetActionDispatcher() { return mActionDispatcher; }
  DiffEngine& GetDiffEngine() { return mDiffEngine; }
  ViewPool& GetViewPool() { return mViewPool; }

  /**
   * Register a component renderer for @p type — the extension point for custom catalogs.
   * Registering a type that already exists (e.g. "Button") overrides the built-in.
   */
  void RegisterComponent(const std::string& type, ComponentRenderFn fn)
  {
    mRegistry.Register(type, std::move(fn));
  }

private:
  /// Register the standard A2UI catalog's built-in component handlers.
  void RegisterStandardCatalog();

  Dali::Ui::View RenderComponent(const std::string& id,
                                 const SurfaceComponentsModel& components,
                                 DataContext& ctx);

  // === Layout & visual components ===
  Dali::Ui::View RenderText(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderFlexContainer(const ComponentModel& comp,
                                     const SurfaceComponentsModel& components,
                                     DataContext& ctx,
                                     Dali::Ui::FlexDirection direction);
  Dali::Ui::View RenderCard(const ComponentModel& comp,
                            const SurfaceComponentsModel& components,
                            DataContext& ctx);
  Dali::Ui::View RenderButton(const ComponentModel& comp,
                              const SurfaceComponentsModel& components,
                              DataContext& ctx);
  Dali::Ui::View RenderImage(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderDivider(const ComponentModel& comp);
  Dali::Ui::View RenderPlaceholder(const ComponentModel& comp);

  // === Input components ===
  Dali::Ui::View RenderTextField(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderCheckBox(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderChoicePicker(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderSlider(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderDateTimeInput(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderProgressBar(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderVideo(const ComponentModel& comp, DataContext& ctx);
  Dali::Ui::View RenderAudioPlayer(const ComponentModel& comp, DataContext& ctx);

  // === Layout containers ===
  Dali::Ui::View RenderTabs(const ComponentModel& comp,
                            const SurfaceComponentsModel& components,
                            DataContext& ctx);
  Dali::Ui::View RenderModal(const ComponentModel& comp,
                             const SurfaceComponentsModel& components,
                             DataContext& ctx);
  Dali::Ui::View RenderList(const ComponentModel& comp,
                            const SurfaceComponentsModel& components,
                            DataContext& ctx);
  Dali::Ui::View RenderIcon(const ComponentModel& comp, DataContext& ctx);

  /// Data-driven child list: children = {path, componentId}. Iterates the data array at
  /// `path`, instantiates `componentId` per element under a per-item scoped DataContext,
  /// and Adds each to outContainer. Returns true if the OBJECT template form was handled
  /// (so the caller must not fall through to the static childIds path).
  bool RenderTemplateChildren(const ComponentModel& comp,
                              const SurfaceComponentsModel& components,
                              DataContext& ctx, Dali::Ui::View outContainer,
                              bool isRow, float gap);

  /// Fill @p container with one @p templateId instance per element of the array at
  /// @p arrayPath, and KEEP IT IN SYNC: the array usually arrives in an updateDataModel
  /// after the components, so a one-shot fill leaves the list permanently empty.
  ///
  /// Rebuilds only when the array's LENGTH changes — a changed field inside an item is
  /// already handled by that item's own bindings, and rebuilding on every keystroke would
  /// throw away the rows (and their input state) the user is looking at.
  ///
  /// @param[in] prepareItem Per-item layout setup (flex sizing, gap), given the item and index
  void BuildTemplateChildren(const std::string& templateId, const std::string& arrayPath,
                             const SurfaceComponentsModel& components, DataContext& ctx,
                             Dali::Ui::View container,
                             std::function<void(Dali::Ui::View item, int index)> prepareItem);

  // === Remote / keyboard focus ===
  /// Make @p view reachable by the TV remote: mark it keyboard-focusable so the
  /// FocusManager can move focus onto it, and run @p onActivate when the focused
  /// view receives the remote's OK/Enter key. Touch activation stays on the
  /// component's own TapGestureDetector — this only adds the key path so both
  /// inputs trigger the same action, keeping the two in sync from one place. The
  /// renderer is a ConnectionTracker, so the key handler is released with it.
  void EnableKeyActivation(Dali::Ui::View view, std::function<void()> onActivate);

  // === Checks validation ===
  void SetupChecks(const ComponentModel& comp, DataContext& ctx,
                   Dali::Ui::Label errorLabel, const std::string& boundPath,
                   Dali::Ui::InputField inputField = Dali::Ui::InputField());

  // === Data binding helpers ===
  std::string ResolveString(const Dali::Ui::Integration::TreeNode* propNode, const DataContext& ctx) const;
  float       ResolveFloat(const Dali::Ui::Integration::TreeNode* propNode, const DataContext& ctx,
                           float fallback = 0.0f) const;
  std::string GetBoundPath(const Dali::Ui::Integration::TreeNode* propNode, const DataContext& ctx) const;

  /// Observers registered while a template generation is being built.
  ///
  /// A generation must be able to retire exactly the watches it created. An id RANGE is not
  /// enough: a NESTED list rebuilds itself later, registering ids outside its parent's
  /// range, and those would survive the parent's rebuild and keep repainting detached views.
  /// So each scope records into itself AND into every enclosing scope.
  struct WatchScope
  {
    std::vector<uint32_t>                 ids;
    std::vector<Dali::TapGestureDetector> tapDetectors; ///< released with the generation
    std::shared_ptr<WatchScope>           parent;
  };
  /// The generation currently being built (null outside BuildTemplateChildren).
  std::shared_ptr<WatchScope> mWatchScope;

  /// Watch @p path, recording the observer in the active generation (if any).
  void RecordWatch(DataModel& model, const std::string& path, DataChangeCallback cb) const;

  /// Keep @p detector alive for as long as the view it was attached to. Inside a template
  /// generation that is the generation's lifetime, not the whole surface's — otherwise a
  /// list that rebuilds keeps a detector per item per rebuild, each holding a dead view.
  void RetainTapDetector(Dali::TapGestureDetector detector);

  /// Drop @p detectors from the retained set (their views are gone).
  void ReleaseTapDetectors(const std::vector<Dali::TapGestureDetector>& detectors);

  /// Keep a rendered property in sync with the data model.
  ///
  /// Watches every path @p propNode depends on and, when one changes, RE-EVALUATES the
  /// whole binding and hands the result to @p apply. Re-evaluating (rather than passing
  /// the raw value at the changed path) is what makes a FunctionCall binding work: the
  /// visible value is `formatCurrency(...)`'s output, not the number the path holds.
  /// @return true if the node is a binding with at least one dependency.
  bool WatchBinding(const Dali::Ui::Integration::TreeNode* propNode, DataContext& ctx,
                    std::function<void(const std::string& value)> apply) const;

  // === Property access helpers ===
  static const char* GetNodeString(const Dali::Ui::Integration::TreeNode& node, const char* key,
                                   const char* fallback = "");
  static float GetNodeFloat(const Dali::Ui::Integration::TreeNode& node, const char* key,
                            float fallback = 0.0f);
  static float VariantToFontSize(const char* variant);
  static Dali::Ui::UiColor ParseHexColor(const char* hex);

  ComponentRegistry  mRegistry;
  std::string        mImageDir;
  bool               mImageThumbnailHint = false; // next Image render is a Row thumbnail
  bool               mAvatarSmallHint    = false; // next avatar Image sits in a Row (chat/list) → small
  // Horizontal width (logical px) currently available to a Text for its line-wrap estimate.
  // Card content is the full width; a Column inside a Row gets only the space left after its
  // leading siblings (image/checkbox/icon), so its text wraps EARLIER. Tracking this stops a
  // multi-line label from reserving too few lines and clipping (the web wraps; we must too).
  // 0 = unset → text.cpp falls back to the full CardContentWidth.
  float              mTextWidthBudget = 0.0f;
  ExpressionParser   mExprParser;
  ActionDispatcher   mActionDispatcher;
  DiffEngine         mDiffEngine;
  ViewPool           mViewPool;

  // Owns TapGestureDetector handles for the current surface. dali-ui has no
  // native Button; we make Button/Card layouts tappable by attaching a
  // detector and must keep the handles alive for as long as the view tree.
  std::vector<Dali::TapGestureDetector> mTapDetectors;

  // Pointer to current components model (valid only during Render call)
  const SurfaceComponentsModel* mCurrentComponents = nullptr;
  DataContext*                   mCurrentCtx = nullptr;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_A2UI_RENDERER_H
