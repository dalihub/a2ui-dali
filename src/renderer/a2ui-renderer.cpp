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
#include <dali/integration-api/debug.h>
#include <dali/public-api/events/key-event.h>
#include <dali/public-api/events/touch-event.h>
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

  // Clear previous observers (from previous render)
  surface.GetDataModel().ClearObservers();

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
  const ComponentModel* comp = components.GetComponent(id);
  if(!comp)
  {
    DALI_LOG_ERROR("[A2UI] Renderer: Component '%s' not found\n", id.c_str());
    // dali-ui 2.5.26 dropped the fluent-chaining setters (they now return void),
    // so configure the placeholder view via individual statements.
    View placeholder = View::New();
    placeholder.SetBackgroundColor(UiColor(0xFF0000));
    placeholder.SetRequestedWidth(MATCH_PARENT);
    placeholder.SetRequestedHeight(20.0f);
    return placeholder;
  }

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
  ctx.GetDataModel().Watch(boundPath,
    [evaluate](const std::string&, const std::string&) mutable { evaluate(); });
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
    ctx.GetDataModel().Watch(dep, reevaluate);
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

} // namespace A2ui
