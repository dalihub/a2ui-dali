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

#include "a2ui-renderer.h"
#include "a11y-bridge.h"
#include "a2ui-theme.h"
#include "a2ui-metrics.h"
#include "render-style.h"
#include "util/shimmer-effect.h"
#include <dali/integration-api/debug.h>
#include <dali/public-api/events/key-event.h>
#include <dali/public-api/events/touch-event.h>
#include <dali/public-api/animation/animation.h>
#include <dali/public-api/animation/time-period.h>
#include <dali/public-api/animation/alpha-function.h>

#include <dali-ui-foundation/public-api/views/image/image-view.h>
#include <dali-ui-foundation/public-api/layouts/flex-layout-params.h>
#include <dali-ui-foundation/public-api/views/scroll/scroll-view.h>
#include <dali-ui-foundation/public-api/text/text-enumerations.h>
#include <dali-ui-foundation/public-api/text/style/underline.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <fstream>

using namespace Dali;
using namespace Dali::Ui;
using Dali::Ui::Integration::TreeNode;

namespace A2ui
{

namespace
{
// Shared OneUI colour constants now live in render-style.h/.cpp (so the per-component
// renderer files share them).

// Recursively walk a component subtree looking for the first node that
// defines an `action`. Used by RenderCard as a fallback so that the whole
// card layout can receive taps — dali-ui has no native Button, so the
// Button's own FlexLayout occasionally fails to surface Clicked events
// (e.g. when nested inside a ScrollView / List template). Having the
// surrounding Card act as a secondary hit target keeps the "Book Now"
// affordance functional from anywhere on the item.

// The remote / keyboard keys that count as "activate the focused item" (the TV
// remote's centre OK button and its keyboard equivalents). Kept in one place so
// every interactive component recognises the same set. "Return" is what the OK
// button reports on Tizen; the others cover keyboards and keypad Enter.
bool IsActivationKey(const std::string& keyName)
{
  return keyName == "Return" || keyName == "KP_Enter" ||
         keyName == "Select" || keyName == "Enter";
}

} // anonymous namespace

A2uiRenderer::A2uiRenderer()
{
  mRevealManager = std::make_unique<MagicRevealManager>();
  RegisterStandardCatalog();
}

void A2uiRenderer::EnableKeyActivation(View view, std::function<void()> onActivate)
{
  if(!view) return;

  // Focusable so the FocusManager can land the remote's focus highlight on it; the
  // OK/Enter key then runs the same action as a tap. Directional navigation between
  // focusable views and the highlight itself are handled by the FocusManager.
  view.SetFocusable(true);
  view.KeyEventSignal().Connect(this,
    [onActivate](View /*view*/, const Dali::KeyEvent& key) -> bool {
      if(key.GetState() == Dali::KeyEvent::DOWN && IsActivationKey(key.GetKeyName().CStr()))
      {
        onActivate();
        return true;  // consumed
      }
      return false;   // let other keys (e.g. arrows) reach the FocusManager
    });
}

// Register the standard A2UI catalog: one entry per built-in component type → its
// renderer. Dispatch in RenderComponent() is a registry lookup over this map (the catalog
// in code); apps extend it for a custom catalog via RegisterComponent(). Each built-in
// adapter forwards to its Render* method, which still draws the actual DALi view tree.
void A2uiRenderer::RegisterStandardCatalog()
{
  using FD = Dali::Ui::FlexDirection;
  mRegistry.Register("Text",          [this](const ComponentModel& c, RenderContext& rc) { return RenderText(c, rc.data); });
  mRegistry.Register("Row",           [this](const ComponentModel& c, RenderContext& rc) { return RenderFlexContainer(c, rc.components, rc.data, FD::ROW); });
  mRegistry.Register("Column",        [this](const ComponentModel& c, RenderContext& rc) { return RenderFlexContainer(c, rc.components, rc.data, FD::COLUMN); });
  mRegistry.Register("Card",          [this](const ComponentModel& c, RenderContext& rc) { return RenderCard(c, rc.components, rc.data); });
  mRegistry.Register("Button",        [this](const ComponentModel& c, RenderContext& rc) { return RenderButton(c, rc.components, rc.data); });
  mRegistry.Register("Image",         [this](const ComponentModel& c, RenderContext& rc) { return RenderImage(c, rc.data); });
  mRegistry.Register("Divider",       [this](const ComponentModel& c, RenderContext& rc) { return RenderDivider(c); });
  mRegistry.Register("TextField",     [this](const ComponentModel& c, RenderContext& rc) { return RenderTextField(c, rc.data); });
  mRegistry.Register("CheckBox",      [this](const ComponentModel& c, RenderContext& rc) { return RenderCheckBox(c, rc.data); });
  mRegistry.Register("ChoicePicker",  [this](const ComponentModel& c, RenderContext& rc) { return RenderChoicePicker(c, rc.data); });
  mRegistry.Register("Slider",        [this](const ComponentModel& c, RenderContext& rc) { return RenderSlider(c, rc.data); });
  mRegistry.Register("DateTimeInput", [this](const ComponentModel& c, RenderContext& rc) { return RenderDateTimeInput(c, rc.data); });
  mRegistry.Register("ProgressBar",   [this](const ComponentModel& c, RenderContext& rc) { return RenderProgressBar(c, rc.data); });
  mRegistry.Register("Video",         [this](const ComponentModel& c, RenderContext& rc) { return RenderVideo(c, rc.data); });
  mRegistry.Register("AudioPlayer",   [this](const ComponentModel& c, RenderContext& rc) { return RenderAudioPlayer(c, rc.data); });
  mRegistry.Register("Tabs",          [this](const ComponentModel& c, RenderContext& rc) { return RenderTabs(c, rc.components, rc.data); });
  mRegistry.Register("Modal",         [this](const ComponentModel& c, RenderContext& rc) { return RenderModal(c, rc.components, rc.data); });
  mRegistry.Register("List",          [this](const ComponentModel& c, RenderContext& rc) { return RenderList(c, rc.components, rc.data); });
  mRegistry.Register("Icon",          [this](const ComponentModel& c, RenderContext& rc) { return RenderIcon(c, rc.data); });
  mRegistry.Register("View",          [this](const ComponentModel& c, RenderContext& rc) { return RenderView(c, rc.data); });
}

// ========================================================================
// Public API
// ========================================================================

View A2uiRenderer::Render(SurfaceModel& surface)
{
  const ComponentModel* root = surface.GetComponentsModel().GetRoot();
  if(!root)
  {
    DALI_LOG_ERROR("[A2UI] Renderer: No 'root' component found\n");
    return View();
  }

  // Clear previous observers (from previous render). The per-generation bookkeeping goes
  // with them — a full render starts outside any template generation.
  surface.GetDataModel().ClearObservers();
  mWatchScope.reset();

  // Release tap detectors from the previous render. The new view tree will
  // attach fresh detectors for its own Button / Card tap targets.
  mTapDetectors.clear();

  // Configure action dispatcher
  mActionDispatcher.SetSurfaceId(surface.GetSurfaceId());
  mActionDispatcher.SetSendDataModel(surface.GetSendDataModel());

  DataContext ctx(surface.GetDataModel());
  mCurrentComponents = &surface.GetComponentsModel();
  mCurrentCtx = &ctx;

  View result;
  try
  {
    result = RenderComponent("root", surface.GetComponentsModel(), ctx);
  }
  catch(const std::exception& e)
  {
    DALI_LOG_ERROR("[A2UI] *** Render() EXCEPTION: %s (surface=%s) ***\n",
                   e.what(), surface.GetSurfaceId().c_str());
    mCurrentComponents = nullptr;
    mCurrentCtx = nullptr;
    throw;
  }

  mCurrentComponents = nullptr;
  mCurrentCtx = nullptr;
  return result;
}

// ========================================================================
// Component dispatch
// ========================================================================

View A2uiRenderer::RenderComponent(const std::string& id,
                                   const SurfaceComponentsModel& components,
                                   DataContext& ctx)
{
  // Partial render mode: if this component ID is NOT in the partial render set,
  // reuse the existing View from the DiffEngine instead of re-rendering it.
  // This lets a parent re-render run its layout logic while only re-rendering
  // the specific child that changed — siblings keep their existing Views.
  if(mPartialRenderMode && mPartialRenderIds.find(id) == mPartialRenderIds.end())
  {
    if(mDiffEngine.HasView(id))
    {
      return mDiffEngine.GetView(id);
    }
    // Not in DiffEngine either — fall through to normal render (new component)
  }

  const ComponentModel* comp = components.GetComponent(id);
  if(!comp)
  {
    DALI_LOG_ERROR("[A2UI] Renderer: Component '%s' not found\n", id.c_str());
    // Shimmer loading animation as the placeholder
    View placeholder = ShimmerEffect::BuildOverlay();
    mDiffEngine.RegisterView(id, placeholder);
    return placeholder;
  }


  // Track which component is being rendered so RecordWatch can associate
  // watchers with this component for later incremental retirement.
  std::string prevRenderingId = mCurrentRenderingId;
  mCurrentRenderingId = id;

  const std::string& type = comp->type;
  // Dispatch through the component registry (the catalog, in code). Unknown types fall
  // back to a placeholder, as the A2UI spec requires.
  View result;
  RenderContext rc{
    ctx, components, mActionDispatcher, mExprParser, mImageDir, mTapDetectors, mImageThumbnailHint,
    [this, &components, &ctx](const std::string& childId) { return RenderComponent(childId, components, ctx); }
  };
  if(const ComponentRenderFn* fn = mRegistry.Get(type))
  {
    result = (*fn)(*comp, rc);
  }
  else
  {
    DALI_LOG_ERROR("[A2UI] Renderer: Unknown component type '%s' (id: %s)\n",
                   type.c_str(), id.c_str());
    result = RenderPlaceholder(*comp);
  }

  // Phase 4: Apply accessibility properties
  if(result)
  {
    A11yBridge::Apply(result, *comp, ctx);
    mDiffEngine.RegisterView(id, result);
  }

  mCurrentRenderingId = prevRenderingId;

  return result;
}


// ========================================================================
// Placeholder
// ========================================================================

View A2uiRenderer::RenderPlaceholder(const ComponentModel& comp)
{
  FlexLayout placeholder = FlexLayout::New();
  placeholder.SetDirection(FlexDirection::ROW);
  placeholder.SetJustifyContent(FlexJustify::CENTER);
  placeholder.SetAlignItems(FlexAlign::CENTER);
  placeholder.SetRequestedWidth(MATCH_PARENT);
  placeholder.SetRequestedHeight(40.0f);
  placeholder.SetBackgroundColor(COLOR_PLACEHOLDER_BG);
  placeholder.SetCornerRadius(Metrics::RadiusCheck());
  placeholder.SetMargin(Extents(0, 0, 2, 2));

  std::string text = "[" + comp.type + ": " + comp.id + "]";
  Label label = Label::New(text.c_str());
  label.SetFontSize(Metrics::FontLabel());
  label.SetTextColor(COLOR_PLACEHOLDER_TEXT);
  label.SetRequestedWidth(WRAP_CONTENT);
  label.SetRequestedHeight(WRAP_CONTENT);
  placeholder.Add(label);

  return placeholder;
}

// ========================================================================
// Checks validation (Phase 2)
// ========================================================================

std::shared_ptr<bool> A2uiRenderer::SetupActionGate(const ComponentModel& comp, DataContext& ctx,
                                                     View view)
{
  auto enabled = std::make_shared<bool>(true);

  const TreeNode* checksNode = comp.rawNode ? comp.rawNode->Find("checks") : nullptr;
  if(!checksNode || checksNode->GetType() != TreeNode::ARRAY)
  {
    return enabled; // no rules declared → never gated
  }

  // Opt this view into dali-ui's default state reaction so that toggling the disabled state
  // below actually repaints it. Applied only to views that declare `checks` — buttons
  // without rules keep exactly the visuals they had.
  if(view) view.SetStateEffect(Dali::Ui::StateEffect::DefaultForInteractive());

  // Re-run every rule and reflect the verdict on both the flag the handlers read and the
  // view's own state. SetEnabled() is dali-ui's disabled token, so the control greys out
  // the same way any other disabled foundation view does — no bespoke dim value here.
  ExpressionParser* parser = &mExprParser;
  auto evaluate = [parser, checksNode, enabled, view, ctx]() mutable {
    bool ok = true;
    for(auto it = checksNode->CBegin(); it != checksNode->CEnd(); ++it)
    {
      const TreeNode* condition = (*it).second.Find("condition");
      if(condition && parser->Evaluate(*condition, ctx) == "false")
      {
        ok = false;
        break;
      }
    }
    *enabled = ok;
    if(view) view.SetEnabled(ok);
  };

  evaluate(); // a form that starts invalid must render with its submit already disabled

  std::vector<std::string> deps;
  for(auto it = checksNode->CBegin(); it != checksNode->CEnd(); ++it)
  {
    if(const TreeNode* condition = (*it).second.Find("condition"))
    {
      mExprParser.CollectDependencyPaths(*condition, ctx, deps);
    }
  }
  for(const std::string& dep : deps)
  {
    RecordWatch(ctx.GetDataModel(), dep,
                [evaluate](const std::string&, const std::string&) mutable { evaluate(); });
  }

  return enabled;
}

void A2uiRenderer::SetupChecks(const ComponentModel& comp, DataContext& ctx,
                                Label errorLabel, const std::string& boundPath,
                                InputField inputField)
{
  const TreeNode* checksNode = comp.rawNode ? comp.rawNode->Find("checks") : nullptr;
  if(!checksNode || checksNode->GetType() != TreeNode::ARRAY)
  {
    return;
  }

  // One evaluation pass: run checks in order, stop at the first failure. Shared by the
  // initial (load-time) evaluation and every later data-model change, so an empty required
  // field shows its error immediately on render — matching the live composer.
  auto evaluate = [this, checksNode, errorLabel, inputField, ctx]() mutable {
    for(auto it = checksNode->CBegin(); it != checksNode->CEnd(); ++it)
    {
      const TreeNode& check = (*it).second;
      const TreeNode* conditionNode = check.Find("condition");
      const TreeNode* messageNode = check.Find("message");

      if(!conditionNode) continue;

      if(mExprParser.Evaluate(*conditionNode, ctx) == "false")
      {
        std::string msg = (messageNode && messageNode->GetType() == TreeNode::STRING)
                          ? messageNode->GetString() : "Validation failed";
        errorLabel.SetText(Dali::String(msg.c_str()));
        errorLabel.SetProperty(Actor::Property::VISIBLE, true);
        if(inputField) inputField.SetBorderlineColor(COLOR_ERROR);  // red invalid outline
        return;
      }
    }
    // All checks passed → restore the normal state
    errorLabel.SetProperty(Actor::Property::VISIBLE, false);
    if(inputField) inputField.SetBorderlineColor(COLOR_INPUT_BORDER);
  };

  evaluate();  // evaluate once at load time (web parity: empty required → error now)

  // Re-evaluate when ANY path a condition reads changes — not just this field's own value.
  // A rule can be about another field ("required(/other)"), and watching only boundPath left
  // such an error on screen after the other field was filled in.
  std::vector<std::string> deps;
  if(!boundPath.empty()) deps.push_back(boundPath);
  for(auto it = checksNode->CBegin(); it != checksNode->CEnd(); ++it)
  {
    if(const TreeNode* condition = (*it).second.Find("condition"))
    {
      mExprParser.CollectDependencyPaths(*condition, ctx, deps);
    }
  }
  for(const std::string& dep : deps)
  {
    RecordWatch(ctx.GetDataModel(), dep,
                [evaluate](const std::string&, const std::string&) mutable { evaluate(); });
  }
}

// ========================================================================
// Data binding helpers
// ========================================================================

std::string A2uiRenderer::ResolveString(const TreeNode* propNode, const DataContext& ctx) const
{
  if(!propNode)
  {
    return "";
  }

  if(propNode->GetType() == TreeNode::STRING)
  {
    return propNode->GetString();
  }

  if(propNode->GetType() == TreeNode::OBJECT)
  {
    // Check "call" first — Find() may recurse into nested args.value.path
    const TreeNode* callNode = propNode->Find("call");
    if(callNode)
    {
      return mExprParser.Evaluate(*propNode, ctx);
    }

    const TreeNode* pathNode = propNode->Find("path");
    if(pathNode && pathNode->GetType() == TreeNode::STRING)
    {
      return ctx.GetString(pathNode->GetString());
    }
  }

  if(propNode->GetType() == TreeNode::INTEGER)
  {
    return std::to_string(propNode->GetInteger());
  }
  if(propNode->GetType() == TreeNode::FLOAT)
  {
    return DataModel::FormatFloatToken(propNode->GetFloat());  // web-style shortest decimal
  }

  return "";
}

float A2uiRenderer::ResolveFloat(const TreeNode* propNode, const DataContext& ctx,
                                 float fallback) const
{
  if(!propNode) return fallback;

  if(propNode->GetType() == TreeNode::FLOAT) return propNode->GetFloat();
  if(propNode->GetType() == TreeNode::INTEGER) return static_cast<float>(propNode->GetInteger());

  if(propNode->GetType() == TreeNode::OBJECT)
  {
    const TreeNode* pathNode = propNode->Find("path");
    if(pathNode && pathNode->GetType() == TreeNode::STRING)
    {
      return ctx.GetFloat(pathNode->GetString(), fallback);
    }
  }

  return fallback;
}

std::string A2uiRenderer::GetBoundPath(const TreeNode* propNode, const DataContext& ctx) const
{
  if(!propNode || propNode->GetType() != TreeNode::OBJECT)
  {
    return "";
  }
  const TreeNode* pathNode = propNode->Find("path");
  if(!pathNode || pathNode->GetType() != TreeNode::STRING)
  {
    return "";
  }
  return ctx.Resolve(pathNode->GetString());
}

void A2uiRenderer::BuildTemplateChildren(const std::string& templateId,
                                         const std::string& arrayPath,
                                         const SurfaceComponentsModel& components,
                                         DataContext& ctx, View container,
                                         std::function<void(View, int)> prepareItem,
                                         std::function<void(View)> onComplete)
{
  // Captured by value / by pointer: the callback outlives this call. The components model
  // and the DataModel both live in the SurfaceModel, and replacing either one goes through
  // a full re-render, which clears the observer this installs.
  const SurfaceComponentsModel* comps = &components;
  DataContext                   scope = ctx;
  A2uiRenderer*                 self  = this;

  // This generation's watches, linked to the generation that created this container so an
  // outer rebuild also retires everything built beneath it.
  auto own    = std::make_shared<WatchScope>();
  own->parent = mWatchScope;
  auto rendered = std::make_shared<int>(-1);

  auto build = [self, templateId, arrayPath, comps, scope, container, prepareItem,
                own, rendered, onComplete]() mutable {
    DataModel&      model = scope.GetDataModel();
    const TreeNode* array = model.ResolvePath(arrayPath);
    int             count = (array && array->GetType() == TreeNode::ARRAY)
                              ? static_cast<int>(array->Size()) : 0;
    if(count == *rendered) return; // same length — each item's own bindings keep it current

    model.UnwatchAll(own->ids);
    own->ids.clear();
    // The views these were attached to are about to go away; without this a list that
    // rebuilds keeps one detector per item per rebuild, each holding a dead view alive.
    self->ReleaseTapDetectors(own->tapDetectors);
    own->tapDetectors.clear();
    while(container.GetChildViewCount() > 0)
    {
      container.Remove(container.GetChildViewAt(0));
    }

    // "/" + index, not arrayPath + "/" + index — a root-level array would otherwise scope
    // its items to "//0", and "//0/name" never matches the "/0/name" an update notifies.
    const std::string base = (arrayPath == "/") ? std::string() : arrayPath;

    std::shared_ptr<WatchScope> saved = self->mWatchScope;
    self->mWatchScope                 = own; // items register into this generation
    for(int i = 0; i < count; ++i)
    {
      DataContext itemCtx = scope.CreateCollectionItemContext(base + "/" + std::to_string(i), i);
      View        item    = self->RenderComponent(templateId, *comps, itemCtx);
      if(item)
      {
        prepareItem(item, i);
        container.Add(item);
      }
    }
    self->mWatchScope = saved;
    *rendered         = count;

    // Fire onComplete after children have been built and added to the container.
    if(onComplete) onComplete(container);
  };

  build(); // first paint — usually empty, the array arrives in a later updateDataModel
  // Recorded in the ENCLOSING generation: this container is one of its children.
  RecordWatch(ctx.GetDataModel(), arrayPath,
              [build](const std::string&, const std::string&) mutable { build(); });
}

void A2uiRenderer::RetainTapDetector(Dali::TapGestureDetector detector)
{
  mTapDetectors.push_back(detector);
  // Inside a template generation, also note it so the generation's rebuild can drop it.
  if(mWatchScope) mWatchScope->tapDetectors.push_back(detector);
}

void A2uiRenderer::ReleaseTapDetectors(const std::vector<Dali::TapGestureDetector>& detectors)
{
  for(const Dali::TapGestureDetector& gone : detectors)
  {
    mTapDetectors.erase(std::remove(mTapDetectors.begin(), mTapDetectors.end(), gone),
                        mTapDetectors.end());
  }
}

void A2uiRenderer::RecordWatch(DataModel& model, const std::string& path,
                               DataChangeCallback cb) const
{
  uint32_t id = model.Watch(path, std::move(cb));
  for(WatchScope* s = mWatchScope.get(); s; s = s->parent.get())
  {
    s->ids.push_back(id);
  }
  // Associate this watcher with the component currently being rendered so
  // an incremental replacement can retire exactly its watchers.
  if(!mCurrentRenderingId.empty())
  {
    mComponentWatchers[mCurrentRenderingId].push_back(id);
  }
}


bool A2uiRenderer::WatchBinding(const TreeNode* propNode, DataContext& ctx,
                                std::function<void(const std::string&)> apply) const
{
  if(!propNode || propNode->GetType() != TreeNode::OBJECT)
  {
    return false;
  }

  std::vector<std::string> deps;
  mExprParser.CollectDependencyPaths(*propNode, ctx, deps);
  if(deps.empty())
  {
    return false;
  }

  // The context is captured BY VALUE: a template row's ctx is a stack temporary in
  // RenderTemplateChildren, but its scope ("/forecast/2") is what makes the row's
  // relative paths resolve, so the callback needs its own copy. The DataModel it
  // references owns the observer list, so it always outlives the callback.
  DataContext         scope = ctx;
  const A2uiRenderer* self  = this;
  auto reevaluate = [self, propNode, scope, apply](const std::string&, const std::string&) mutable {
    apply(self->ResolveString(propNode, scope));
  };

  for(const std::string& dep : deps)
  {
    RecordWatch(ctx.GetDataModel(), dep, reevaluate);
  }
  return true;
}

// ========================================================================
// Helpers
// ========================================================================

const char* A2uiRenderer::GetNodeString(const TreeNode& node, const char* key, const char* fallback)
{
  const TreeNode* child = node.Find(key);
  if(child && child->GetType() == TreeNode::STRING)
  {
    return child->GetString();
  }
  return fallback;
}

float A2uiRenderer::GetNodeFloat(const TreeNode& node, const char* key, float fallback)
{
  const TreeNode* child = node.Find(key);
  if(child)
  {
    if(child->GetType() == TreeNode::FLOAT) return child->GetFloat();
    if(child->GetType() == TreeNode::INTEGER) return static_cast<float>(child->GetInteger());
  }
  return fallback;
}

float A2uiRenderer::VariantToFontSize(const char* variant)
{
  if(strcmp(variant, "h1") == 0) return Metrics::FontH1();
  if(strcmp(variant, "h2") == 0) return Metrics::FontH2();
  if(strcmp(variant, "h3") == 0) return Metrics::FontH3();
  if(strcmp(variant, "h4") == 0) return Metrics::FontH4();
  if(strcmp(variant, "h5") == 0) return Metrics::FontH5();
  if(strcmp(variant, "caption") == 0) return Metrics::FontCaption();
  return Metrics::FontBody(); // body (default)
}

UiColor A2uiRenderer::ParseHexColor(const char* hex)
{
  if(!hex || hex[0] == '\0') return UiColor(0x000000);
  const char* p = (hex[0] == '#') ? hex + 1 : hex;
  uint32_t val = static_cast<uint32_t>(strtoul(p, nullptr, 16));
  return UiColor(val);
}

// ========================================================================
// Stagger reveal — fade-in children one by one instead of all at once
// ========================================================================
void A2uiRenderer::StaggerRevealChildren(View container, float staggerSec, float fadeSec)
{
  if(!container) return;

  // Prune finished animations so the vector doesn't grow unboundedly across
  // rebuilds. A STOPPED animation has either completed or never been played.
  mStaggerAnimations.erase(
    std::remove_if(mStaggerAnimations.begin(), mStaggerAnimations.end(),
      [](const Animation& a) { return !a || a.GetState() == Animation::STOPPED; }),
    mStaggerAnimations.end());

  const uint32_t childCount = container.GetChildViewCount();
  if(childCount == 0) return;

  // One Animation holds all per-child AnimateTo with staggered TimePeriods so they
  // overlap within a single timeline (cleaner than N separate Animations whose
  // lifetimes we'd have to track individually).
  float totalDuration = staggerSec * static_cast<float>(childCount - 1) + fadeSec;
  if(totalDuration <= 0.0f) return;

  Animation anim = Animation::New(totalDuration);
  anim.SetEndAction(Animation::BAKE);       // bake final opacity=1.0 so it sticks after play
  anim.SetDisconnectAction(Animation::BAKE);

  for(uint32_t i = 0; i < childCount; ++i)
  {
    View child = container.GetChildViewAt(i);
    if(!child) continue;

    // Start invisible — the fade-in brings it from 0 → 1.
    child.SetProperty(Actor::Property::OPACITY, 0.0f);

    // Each child begins its fade at i*staggerSec and lasts fadeSec.
    float delay = staggerSec * static_cast<float>(i);
    anim.AnimateTo(Property(child, Actor::Property::OPACITY), 1.0f,
                   AlphaFunction::EASE_OUT,
                   TimePeriod(delay, fadeSec));
  }

  anim.Play();
  mStaggerAnimations.push_back(anim);  // keep alive for the duration of playback
}


// ========================================================================
// Incremental rendering (AI-generated)
// ========================================================================

void A2uiRenderer::InvalidateRenderState()
{
  mDiffEngine.Clear();
  mComponentWatchers.clear();
  mWatchScope.reset();
}

View A2uiRenderer::RenderIncremental(SurfaceModel& surface)
{
  // No previous render → full render
  if(!IsRendered())
  {
    std::printf("[A2UI] *** Full render ***\n");
    return Render(surface);
  }

  const SurfaceComponentsModel& components = surface.GetComponentsModel();
  const auto& changedIds = components.GetLastModifiedIds();

  // Root itself changed → full re-render
  for(const auto& id : changedIds)
  {
    if(id == "root")
    {
      std::printf("[A2UI] *** Full render ***\n");
      return Render(surface);
    }
  }
  std::printf("[A2UI] *** RenderIncremental ***\n");
  // Configure action dispatcher (same as full Render)
  mActionDispatcher.SetSurfaceId(surface.GetSurfaceId());
  mActionDispatcher.SetSendDataModel(surface.GetSendDataModel());

  DataContext ctx(surface.GetDataModel());
  mCurrentComponents = &components;
  mCurrentCtx = &ctx;

  try
  {
    ApplyIncrementalUpdates(surface, changedIds, ctx);
  }
  catch(const std::exception& e)
  {
    DALI_LOG_ERROR("[A2UI] *** RenderIncremental() EXCEPTION: %s ***\n", e.what());
    mCurrentComponents = nullptr;
    mCurrentCtx = nullptr;
    throw;
  }

  mCurrentComponents = nullptr;
  mCurrentCtx = nullptr;

  // Return the current root view (unchanged object — caller need not re-mount)
  return mDiffEngine.GetView("root");
}

void A2uiRenderer::ApplyIncrementalUpdates(SurfaceModel& surface,
                                            const std::vector<std::string>& changedIds,
                                            DataContext& ctx)
{
  const SurfaceComponentsModel& components = surface.GetComponentsModel();

  // Filter to top-level changes: if an ancestor of a changed component is also
  // changed, the ancestor's rebuild will cover the descendant.
  std::vector<std::string> topLevel = FilterTopLevelChanged(changedIds, components);

  // Track which IDs already existed before this update (for Add vs Replace decision)
  // An ID is "new" if the DiffEngine has no view for it yet.
  for(const std::string& id : topLevel)
  {
    bool isNew = !mDiffEngine.HasView(id);
    UpdateOneComponent(surface, id, ctx, isNew);
  }

  // Update snapshot so next diff is relative to the current state
  mDiffEngine.TakeSnapshot(components);
}

void A2uiRenderer::UpdateOneComponent(SurfaceModel& surface, const std::string& componentId,
                                      DataContext& ctx, bool isNew)
{
  const SurfaceComponentsModel& components = surface.GetComponentsModel();
  const ComponentModel* comp = components.GetComponent(componentId);
  if(!comp)
  {
    DALI_LOG_ERROR("[A2UI] Incremental: component '%s' not found\n", componentId.c_str());
    return;
  }

  // If this component needs its parent to be re-rendered, skip the initial
  // render+splice — go directly to RerenderParent, which re-renders the parent
  // (and only this child via partial render mode). This avoids redundant work:
  // the initial render would be thrown away when RerenderParent creates a new
  // parent view that includes a fresh copy of this child.
  //
  // EXCEPTION: if the parent is "root", RerenderParent skips it (root has no
  // grandparent to splice into). Fall through to the normal in-place replace
  // path instead — the leaf component's own WatchBinding handles its content
  // update, and root's layout is already correct.
  if(comp->needsParentRerender)
  {
    std::string parentId = components.GetParentId(componentId);
    if(parentId.empty())
    {
      DALI_LOG_ERROR("[A2UI] Incremental: component '%s' has no parent for re-render\n", componentId.c_str());
      return;
    }
    if(parentId != "root")
    {
      // Retire watchers for this component before the parent re-renders it.
      RetireComponentWatchers(surface.GetDataModel(), componentId);
      RerenderParent(surface, parentId, ctx, componentId);
      return;
    }
    // Parent is root — fall through to normal in-place replace path.
  }

  // Capture the old view BEFORE RenderComponent — RenderComponent calls
  // RegisterView() internally, which would overwrite the old mapping.
  View oldView = isNew ? View() : mDiffEngine.GetView(componentId);

  // Retire old watchers registered by this component (and its descendants).
  // RenderComponent will re-register fresh ones for the new subtree.
  RetireComponentWatchers(surface.GetDataModel(), componentId);

  // For a container component being replaced in-place, set partial render mode
  // so that children NOT in the changed set reuse their existing Views from
  // the DiffEngine instead of being re-rendered from scratch. Only this
  // component (and its newly-arrived children) should be rendered fresh.
  // This is the same optimization RerenderParent uses, applied to the
  // in-place replace path (e.g. when the parent is root).
  bool wasPartialRender = mPartialRenderMode;
  auto savedPartialIds = mPartialRenderIds;
  mPartialRenderMode = true;
  mPartialRenderIds.clear();
  mPartialRenderIds.insert(componentId);
  // Also include any children that are in this update's modified set — they
  // need fresh renders too. The component itself is always re-rendered.

  // Render the new subtree
  View newView = RenderComponent(componentId, components, ctx);

  // Restore previous partial render state.
  mPartialRenderMode = wasPartialRender;
  mPartialRenderIds = savedPartialIds;
  if(!newView)
  {
    DALI_LOG_ERROR("[A2UI] Incremental: RenderComponent returned null for '%s'\n", componentId.c_str());
    return;
  }

  if(isNew)
  {
    // New component: find its parent and Add it at the right position
    std::string parentId = components.GetParentId(componentId);
    if(parentId.empty())
    {
      // No parent found — can't splice a new root-level component
      DALI_LOG_ERROR("[A2UI] Incremental: new component '%s' has no parent\n", componentId.c_str());
      return;
    }

    View parentView = mDiffEngine.GetView(parentId);
    if(!parentView)
    {
      DALI_LOG_ERROR("[A2UI] Incremental: parent view '%s' not found\n", parentId.c_str());
      return;
    }

    int index = components.GetChildIndexInParent(componentId);
    if(index >= 0 && static_cast<uint32_t>(index) < parentView.GetChildViewCount())
    {
      parentView.Add(newView);
    }
    else
    {
      // Append at end (common case for template children growth)
      parentView.Add(newView);
    }
    // Leaf component: no parent re-render needed — the Add above is sufficient.
  }
  else
  {
    // Existing component: replace the old view in-place
    if(!oldView)
    {
      DALI_LOG_ERROR("[A2UI] Incremental: old view for '%s' not in DiffEngine\n", componentId.c_str());
      return;
    }

    // Copy layout params so the replacement keeps its slot
    CopyLayoutParams(oldView, newView);

    // Find the parent container and replace
    std::string parentId = components.GetParentId(componentId);
    if(parentId.empty())
    {
      // This is root or an orphaned component — can't splice
      DALI_LOG_ERROR("[A2UI] Incremental: component '%s' has no parent for replace\n", componentId.c_str());
      return;
    }

    View parentView = mDiffEngine.GetView(parentId);
    if(!parentView)
    {
      DALI_LOG_ERROR("[A2UI] Incremental: parent view '%s' not found for replace\n", parentId.c_str());
      return;
    }

    // Find the old view's position in parent and replace it
    uint32_t childCount = parentView.GetChildViewCount();
    for(uint32_t i = 0; i < childCount; ++i)
    {
      if(parentView.GetChildViewAt(i) == oldView)
      {
        // Remove old, insert new at same position.
        // dali-ui doesn't have a Replace method, so Remove + re-Add children.
        // Collect children after the removed position
        std::vector<View> after;
        for(uint32_t j = i + 1; j < childCount; ++j)
        {
          after.push_back(parentView.GetChildViewAt(j));
        }
        // Remove old view and all after it
        parentView.Remove(oldView);
        for(const auto& v : after)
        {
          parentView.Remove(v);
        }
        // Add new view at position i
        parentView.Add(newView);
        // Re-Add the rest
        for(const auto& v : after)
        {
          parentView.Add(v);
        }
        break;
      }
    }
    // Leaf component: no parent re-render needed — the replace above is sufficient.
  }
}

void A2uiRenderer::RerenderParent(SurfaceModel& surface, const std::string& parentId,
                                  DataContext& ctx, const std::string& changedChildId)
{
  const SurfaceComponentsModel& components = surface.GetComponentsModel();
  const ComponentModel* parentComp = components.GetComponent(parentId);
  if(!parentComp) return;

  // The root container has no grandparent to splice into, and the host's render
  // callback keys off the DiffEngine's root view object identity. Re-rendering
  // root here would create a new root view in the DiffEngine (breaking identity)
  // without actually placing it on stage. Skip — root-level layout is already
  // correct because root is re-rendered as a whole on any root component change.
  if(parentId == "root") return;

  View parentOldView = mDiffEngine.GetView(parentId);
  if(!parentOldView) return;

  // Retire watchers for the parent AND all its descendants (the re-render
  // rebuilds the whole subtree).
  RetireComponentWatchers(surface.GetDataModel(), parentId);
  for(const auto& cid : parentComp->childIds)
  {
    RetireComponentWatchers(surface.GetDataModel(), cid);
  }
  if(!parentComp->childId.empty())
  {
    RetireComponentWatchers(surface.GetDataModel(), parentComp->childId);
  }

  // Partial render mode: when changedChildId is specified, only the parent and
  // that child are re-rendered. Other children reuse their existing Views from
  // the DiffEngine (RenderComponent checks mPartialRenderMode and returns the
  // cached View for IDs not in mPartialRenderIds).
  if(!changedChildId.empty())
  {
    mPartialRenderMode = true;
    mPartialRenderIds.clear();
    mPartialRenderIds.insert(parentId);
    mPartialRenderIds.insert(changedChildId);
  }

  View parentNewView = RenderComponent(parentId, components, ctx);

  // Always clear partial render mode after the parent render completes.
  mPartialRenderMode = false;
  mPartialRenderIds.clear();

  if(!parentNewView) return;

  CopyLayoutParams(parentOldView, parentNewView);

  // Splice the new parent view into the grandparent, replacing the old one.
  std::string grandParentId = components.GetParentId(parentId);
  if(grandParentId.empty()) return;

  View grandParentView = mDiffEngine.GetView(grandParentId);
  if(!grandParentView) return;

  uint32_t gc = grandParentView.GetChildViewCount();
  for(uint32_t i = 0; i < gc; ++i)
  {
    if(grandParentView.GetChildViewAt(i) == parentOldView)
    {
      std::vector<View> after;
      for(uint32_t j = i + 1; j < gc; ++j)
        after.push_back(grandParentView.GetChildViewAt(j));
      grandParentView.Remove(parentOldView);
      for(const auto& v : after) grandParentView.Remove(v);
      grandParentView.Add(parentNewView);
      for(const auto& v : after) grandParentView.Add(v);
      break;
    }
  }
}

void A2uiRenderer::CopyLayoutParams(View oldView, View newView)
{
  if(!oldView || !newView) return;

  // Copy margin (parent-assigned layout spacing, not component-declared size)
  newView.SetMargin(oldView.GetMargin());

  // Copy FlexLayoutParams if present (flex grow/shrink/basis from parent)
  FlexLayoutParams flexParams;
  if(oldView.TryGetLayoutParams(flexParams))
  {
    newView.SetLayoutParams(flexParams);
  }

  // NOTE: Do NOT copy requested width/height — the new view already has its
  // own dimensions set from the JSON component definition. Copying the old
  //
}

void A2uiRenderer::RetireComponentWatchers(DataModel& model, const std::string& componentId)
{
  auto it = mComponentWatchers.find(componentId);


  if(it == mComponentWatchers.end()) return;

  model.UnwatchAll(it->second);
  mComponentWatchers.erase(it);
}

std::vector<std::string> A2uiRenderer::FilterTopLevelChanged(
  const std::vector<std::string>& changedIds,
  const SurfaceComponentsModel& components) const
{
  // Build a set for O(1) lookup
  std::unordered_set<std::string> changedSet(changedIds.begin(), changedIds.end());

  std::vector<std::string> result;
  for(const std::string& id : changedIds)
  {
    // Walk up the parent chain; if any ancestor is also in changedSet,
    // this component is covered by the ancestor's rebuild → skip it.
    bool coveredByAncestor = false;
    std::string parentId = components.GetParentId(id);
    while(!parentId.empty())
    {
      if(changedSet.count(parentId))
      {
        coveredByAncestor = true;
        break;
      }
      parentId = components.GetParentId(parentId);
    }

    if(!coveredByAncestor)
    {
      result.push_back(id);
    }
  }
  return result;
}


} // namespace A2ui

