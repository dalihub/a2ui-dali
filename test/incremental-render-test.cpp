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

/**
 * Incremental render test for a2ui-dali.
 *
 * Drives the real A2uiHost + renderer through updateComponents messages
 * and verifies that:
 *   1. The first updateComponents triggers a full Render()
 *   2. Subsequent updateComponents (non-root) trigger RenderIncremental
 *   3. The text rendered after an incremental update is correct
 *   4. The root view object identity is preserved across incremental updates
 *   5. Surfaces sharing component ids keep separate views, siblings of a re-rendered
 *      parent keep their data bindings, and a re-sent template List keeps every item
 *
 * Needs a display (builds actual DALi actors) — run it under Xvfb:
 *   tools/run-tests.sh
 */

#include "renderer/a2ui-host.h"

#include <dali/public-api/adaptor-framework/application.h>
#include <dali/public-api/adaptor-framework/timer.h>
#include <dali/public-api/adaptor-framework/window-data.h>
#include <dali/public-api/math/rect.h>
#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali-ui-foundation/public-api/views/text-controls/label.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using Dali::Ui::Label;
using Dali::Ui::View;

namespace
{
int gTotal  = 0;
int gPassed = 0;
int gFailed = 0;


void ReportTest(const std::string& name, bool passed, const std::string& error = "")
{
  gTotal++;
  if(passed)
  {
    gPassed++;
    std::cout << "  PASS: " << name << std::endl;
  }
  else
  {
    gFailed++;
    std::cout << "  FAIL: " << name << " — " << error << std::endl;
  }
}

/// Recursively collect all Label text from a view tree (DFS).
void CollectLabels(View view, std::vector<std::string>& out)
{
  if(!view) return;
  if(!view.GetProperty<bool>(Dali::Actor::Property::VISIBLE)) return;
  Label label = Label::DownCast(view);
  if(label)
  {
    out.push_back(std::string(label.GetText().CStr()));
  }
  for(uint32_t i = 0; i < view.GetChildViewCount(); ++i)
  {
    CollectLabels(view.GetChildViewAt(i), out);
  }
}

std::vector<std::string> GetTexts(View root)
{
  std::vector<std::string> texts;
  CollectLabels(root, texts);
  return texts;
}

// ========================================================================
// Test 1: Incremental text update — second updateComponents changes only
//         one Text component; root view identity must be preserved.
// ========================================================================
void TestIncrementalTextUpdate(A2ui::A2uiHost& host, View& rootView)
{
  std::cout << "\n=== Test: Incremental Text Update ===" << std::endl;

  // Feed createSurface + first updateComponents
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"inc","catalogId":"basic"}})");

  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"inc","components":[
    {"id":"root","component":"Column","children":["title","body"]},
    {"id":"title","component":"Text","text":"Original Title"},
    {"id":"body","component":"Text","text":"Original Body"}
  ]}})");

  // The host's OnRender callback should have given us the root view.
  View firstRoot = rootView;
  ReportTest("first render produces a root view", !!firstRoot, "root view is null");

  if(!firstRoot) return;

  // Verify initial text
  std::vector<std::string> texts = GetTexts(firstRoot);
  ReportTest("initial text has 2 labels", texts.size() == 2,
             "expected 2, got " + std::to_string(texts.size()));
  if(texts.size() >= 2)
  {
    ReportTest("initial title is 'Original Title'", texts[0] == "Original Title",
               "got '" + texts[0] + "'");
    ReportTest("initial body is 'Original Body'", texts[1] == "Original Body",
               "got '" + texts[1] + "'");
  }

  // Now send a second updateComponents that only changes "body"
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"inc","components":[
    {"id":"body","component":"Text","text":"Updated Body"}
  ]}})");

  // The root view should be the SAME object (incremental, not full re-render)

  View secondRoot = rootView;
  ReportTest("root view identity preserved after incremental", secondRoot == firstRoot,
             "root view changed — full re-render happened instead of incremental");


  // The body text should be updated
  std::vector<std::string> texts2 = GetTexts(secondRoot);
  ReportTest("after incremental: 2 labels", texts2.size() == 2,
             "expected 2, got " + std::to_string(texts2.size()));
  if(texts2.size() >= 2)
  {
    ReportTest("after incremental: title unchanged", texts2[0] == "Original Title",
               "got '" + texts2[0] + "'");
    ReportTest("after incremental: body is 'Updated Body'", texts2[1] == "Updated Body",
               "got '" + texts2[1] + "'");
  }
}

// ========================================================================
// Test 2: Incremental component addition — add a new Text to the root's
//         children list.
// ========================================================================
void TestIncrementalComponentAdd(A2ui::A2uiHost& host, View& rootView)
{
  std::cout << "\n=== Test: Incremental Component Addition ===" << std::endl;

  // Reset: create a fresh surface
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"inc2","catalogId":"basic"}})");

  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"inc2","components":[
    {"id":"root","component":"Column","children":["a"]},
    {"id":"a","component":"Text","text":"A"}
  ]}})");

  View firstRoot = rootView;
  ReportTest("initial render for add test", !!firstRoot);
  if(!firstRoot) return;

  std::vector<std::string> texts = GetTexts(firstRoot);
  ReportTest("initial: 1 label", texts.size() == 1,
             "expected 1, got " + std::to_string(texts.size()));

  // Add a new component "b" and update root to include it
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"inc2","components":[
    {"id":"root","component":"Column","children":["a","b"]},
    {"id":"b","component":"Text","text":"B"}
  ]}})");

  // Root changed → this should fall back to full Render()
  View secondRoot = rootView;

  // After adding "b", we should have 2 labels
  std::vector<std::string> texts2 = GetTexts(secondRoot);
  ReportTest("after add: 2 labels", texts2.size() == 2,
             "expected 2, got " + std::to_string(texts2.size()));
  if(texts2.size() >= 2)
  {
    ReportTest("after add: label A present",
               std::find(texts2.begin(), texts2.end(), "A") != texts2.end());
    ReportTest("after add: label B present",
               std::find(texts2.begin(), texts2.end(), "B") != texts2.end());
  }
}

// ========================================================================
// Test 3: IsRendered / InvalidateRenderState
// ========================================================================
void TestRenderState(A2ui::A2uiHost& host, View& /*rootView*/)
{
  std::cout << "\n=== Test: Render State ===" << std::endl;

  A2ui::A2uiRenderer& renderer = host.GetRenderer();

  // After previous tests, renderer should be in rendered state
  ReportTest("renderer IsRendered() after a render", renderer.IsRendered(),
             "expected IsRendered()=true");

  // Invalidate
  renderer.InvalidateRenderState();
  ReportTest("after InvalidateRenderState: IsRendered()=false", !renderer.IsRendered(),
             "expected IsRendered()=false after invalidation");

  // Next render should go through full Render() (not incremental)
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"inc3","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"inc3","components":[
    {"id":"root","component":"Column","children":["x"]},
    {"id":"x","component":"Text","text":"X"}
  ]}})");

  ReportTest("after re-render: IsRendered()=true", renderer.IsRendered(),
             "expected IsRendered()=true after re-render");
}


std::string Joined(View root)
{
  std::string out;
  for(const std::string& t : GetTexts(root)) out += (out.empty() ? "" : ",") + t;
  return out;
}

// ========================================================================
// Test 4: Two surfaces both have a "root". A non-root update to the first
//         surface must render the first surface, not the second one's tree.
// ========================================================================
void TestTwoSurfaces(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Two Surfaces ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"twoA","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"twoA","components":[
    {"id":"root","component":"Column","children":["a1"]},
    {"id":"a1","component":"Text","text":"A-one"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"twoB","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"twoB","components":[
    {"id":"root","component":"Column","children":["b1"]},
    {"id":"b1","component":"Text","text":"B-one"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"twoA","components":[
    {"id":"a1","component":"Text","text":"A-two"}
  ]}})");

  ReportTest("surface A shows its own update", Joined(views["twoA"]) == "A-two",
             "got '" + Joined(views["twoA"]) + "'");
  ReportTest("surface B is untouched", Joined(views["twoB"]) == "B-one",
             "got '" + Joined(views["twoB"]) + "'");
  ReportTest("surfaces keep separate views", views["twoA"] != views["twoB"],
             "both surfaces were handed the same view");

  // Re-creating a deleted surface under the same id starts from a full render.
  host.JsonFeed(R"({"version":"v0.9","deleteSurface":{"surfaceId":"twoB"}})");
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"twoB","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"twoB","components":[
    {"id":"root","component":"Column","children":["b2"]},
    {"id":"b2","component":"Text","text":"B-new"}
  ]}})");
  ReportTest("re-created surface renders only its new tree", Joined(views["twoB"]) == "B-new",
             "got '" + Joined(views["twoB"]) + "'");
}

// ========================================================================
// Test 5: Re-rendering a parent for one changed child keeps the unchanged
//         siblings bound to the data model.
// ========================================================================
void TestSiblingBindings(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Sibling Bindings Survive A Parent Re-render ===" << std::endl;

  // Sibling updated after the first render.
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"sib","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"sib","components":[
    {"id":"root","component":"Column","children":["sec"]},
    {"id":"sec","component":"Column","children":["price","status"]},
    {"id":"price","component":"Text","text":{"path":"/price"}},
    {"id":"status","component":"Text","text":"pending"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"sib","path":"/price","value":"100"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"sib","components":[
    {"id":"status","component":"Text","text":"done"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"sib","path":"/price","value":"200"}})");
  ReportTest("updated sibling: bound text follows the data model", Joined(views["sib"]) == "200,done",
             "got '" + Joined(views["sib"]) + "'");

  // Sibling arriving late (the streaming order).
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"late","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"late","components":[
    {"id":"root","component":"Column","children":["sec"]},
    {"id":"sec","component":"Column","children":["price","status"]},
    {"id":"price","component":"Text","text":{"path":"/price"}}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"late","path":"/price","value":"100"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"late","components":[
    {"id":"status","component":"Text","text":"in stock"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"late","path":"/price","value":"200"}})");
  ReportTest("late sibling: bound text follows the data model", Joined(views["late"]) == "200,in stock",
             "got '" + Joined(views["late"]) + "'");
}

// ========================================================================
// Test 6: Re-sending a template List keeps one view per data item.
// ========================================================================
void TestListResend(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Template List Re-sent ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"lst","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"lst","components":[
    {"id":"root","component":"Column","children":["hdr","list"]},
    {"id":"hdr","component":"Text","text":"Items"},
    {"id":"list","component":"List","children":{"componentId":"item","path":"/items"}},
    {"id":"item","component":"Text","text":{"path":"name"}}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"lst","path":"/items","value":[{"name":"a"},{"name":"b"},{"name":"c"}]}})");
  ReportTest("list shows every item", Joined(views["lst"]) == "Items,a,b,c",
             "got '" + Joined(views["lst"]) + "'");

  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"lst","components":[
    {"id":"list","component":"List","children":{"componentId":"item","path":"/items"}}
  ]}})");
  ReportTest("re-sent list still shows every item", Joined(views["lst"]) == "Items,a,b,c",
             "got '" + Joined(views["lst"]) + "'");
}

// ========================================================================
// Test 7: A List rebuild on one surface must not touch another surface's
//         incremental state, even when component ids collide.
// ========================================================================
void TestListRebuildAcrossSurfaces(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: List Rebuild Across Surfaces ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"xA","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"xA","components":[
    {"id":"root","component":"Column","children":["lst"]},
    {"id":"lst","component":"List","children":{"componentId":"item","path":"/items"}},
    {"id":"item","component":"Text","text":{"path":"name"}}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"xA","path":"/items","value":[{"name":"a"}]}})");

  // Surface B uses "item" for an ordinary container.
  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"xB","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"xB","components":[
    {"id":"root","component":"Column","children":["item"]},
    {"id":"item","component":"Column","children":["p","st"]},
    {"id":"p","component":"Text","text":{"path":"/p"}},
    {"id":"st","component":"Text","text":"pending"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"xB","path":"/p","value":"100"}})");

  // A's list rebuilds while B owns the incremental state.
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"xA","path":"/items","value":[{"name":"a"},{"name":"b"}]}})");
  ReportTest("list on surface A rebuilt", Joined(views["xA"]) == "a,b", "got '" + Joined(views["xA"]) + "'");

  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"xB","components":[
    {"id":"st","component":"Text","text":"done"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"xB","path":"/p","value":"200"}})");
  ReportTest("surface B unaffected by A's list rebuild", Joined(views["xB"]) == "200,done",
             "got '" + Joined(views["xB"]) + "'");
}

// ========================================================================
// Test 8: Re-sending a List retires its previous items' watches.
// ========================================================================
void TestListResendReleasesItems(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Re-sent List Releases Its Old Items ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"rel","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"rel","components":[
    {"id":"root","component":"Column","children":["hdr","list"]},
    {"id":"hdr","component":"Text","text":"Items"},
    {"id":"list","component":"List","children":{"componentId":"item","path":"/items"}},
    {"id":"item","component":"Text","text":{"path":"name"}}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"rel","path":"/items","value":[{"name":"a"},{"name":"b"},{"name":"c"}]}})");

  A2ui::SurfaceModel* surface = host.GetSurfaces().GetSurface("rel");
  size_t before = surface ? surface->GetDataModel().ObserverCount() : 0;
  for(int i = 0; i < 3; ++i)
  {
    host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"rel","components":[
      {"id":"list","component":"List","children":{"componentId":"item","path":"/items"}}
    ]}})");
  }
  size_t after = surface ? surface->GetDataModel().ObserverCount() : 0;
  ReportTest("observer count stays flat across re-sends", before > 0 && after == before,
             "before " + std::to_string(before) + ", after " + std::to_string(after));

  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"rel","path":"/items/0/name","value":"Z"}})");
  ReportTest("re-sent list items stay bound", Joined(views["rel"]) == "Items,Z,b,c",
             "got '" + Joined(views["rel"]) + "'");
}

// ========================================================================
// Test 9: A component that contains itself renders without recursing forever.
// ========================================================================
void TestSelfContainingComponent(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Self-containing Component ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"self","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"self","components":[
    {"id":"root","component":"Column","children":["a"]},
    {"id":"a","component":"Text","text":"A"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"self","components":[
    {"id":"a","component":"Column","children":["a"]}
  ]}})");
  ReportTest("incremental self-reference renders", !!views["self"], "no view");

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"self2","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"self2","components":[
    {"id":"root","component":"Column","children":["b"]},
    {"id":"b","component":"Column","children":["c"]},
    {"id":"c","component":"Column","children":["b"]}
  ]}})");
  ReportTest("full render of a cycle renders", !!views["self2"], "no view");
}

// ========================================================================
// Test 10: A template component re-sent updates every place it is used:
//          each List sharing it, and an ordinary use of the same id.
// ========================================================================
void TestSharedTemplate(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Shared Template Re-sent ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"shr","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"shr","components":[
    {"id":"root","component":"Column","children":["l1","l2","it"]},
    {"id":"l1","component":"List","children":{"componentId":"it","path":"/a"}},
    {"id":"l2","component":"List","children":{"componentId":"it","path":"/b"}},
    {"id":"it","component":"Text","text":"old"}
  ]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"shr","path":"/a","value":[1]}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"shr","path":"/b","value":[1]}})");
  ReportTest("shared template before", Joined(views["shr"]) == "old,old,old",
             "got '" + Joined(views["shr"]) + "'");

  host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"shr","components":[
    {"id":"it","component":"Text","text":"new"}
  ]}})");
  ReportTest("every use of the re-sent template updates", Joined(views["shr"]) == "new,new,new",
             "got '" + Joined(views["shr"]) + "'");
}

// ========================================================================
// Test 11: The inline children.template List releases its old items on re-send.
// ========================================================================
void TestInlineTemplateResend(A2ui::A2uiHost& host, std::map<std::string, View>& views)
{
  std::cout << "\n=== Test: Inline Template List Re-sent ===" << std::endl;

  host.JsonFeed(R"({"version":"v0.9","createSurface":{"surfaceId":"inl","catalogId":"basic"}})");
  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"inl","path":"/rows","value":[{"n":"1"},{"n":"2"}]}})");
  const char* list = R"({"version":"v0.9","updateComponents":{"surfaceId":"inl","components":[
    {"id":"root","component":"Column","children":["lst"]},
    {"id":"lst","component":"List","children":{"template":{"dataBinding":"/rows",
      "components":[{"id":"row","component":"Text","text":{"path":"n"}}]}}}
  ]}})";
  host.JsonFeed(list);

  A2ui::SurfaceModel* surface = host.GetSurfaces().GetSurface("inl");
  size_t before = surface ? surface->GetDataModel().ObserverCount() : 0;
  for(int i = 0; i < 3; ++i)
  {
    host.JsonFeed(R"({"version":"v0.9","updateComponents":{"surfaceId":"inl","components":[
      {"id":"lst","component":"List","children":{"template":{"dataBinding":"/rows",
        "components":[{"id":"row","component":"Text","text":{"path":"n"}}]}}}
    ]}})");
  }
  size_t after = surface ? surface->GetDataModel().ObserverCount() : 0;
  ReportTest("inline template: observer count stays flat", before > 0 && after == before,
             "before " + std::to_string(before) + ", after " + std::to_string(after));

  host.JsonFeed(R"({"version":"v0.9","updateDataModel":{"surfaceId":"inl","path":"/rows/0/n","value":"Z"}})");
  ReportTest("inline template items stay bound", Joined(views["inl"]) == "Z,2",
             "got '" + Joined(views["inl"]) + "'");
}

} // anonymous namespace

class TestApp : public Dali::ConnectionTracker
{
public:
  TestApp(Dali::Application& app)
  : mApplication(app)
  {
    mApplication.InitSignal().Connect(this, &TestApp::OnInit);
  }

private:
  void OnInit(Dali::Application app)
  {
    std::cout << "========================================" << std::endl;
    std::cout << "  a2ui-dali incremental render test" << std::endl;
    std::cout << "========================================" << std::endl;

    // Set up the host with a render callback that captures the root view
    mHost.GetRenderer().SetImageDir("res/");
    mHost.SetOnBeginRenderingSurface([this](const std::string& surfaceId, View view) {
      mRootView         = view;
      mViews[surfaceId] = view;
    });

    TestIncrementalTextUpdate(mHost, mRootView);
    TestIncrementalComponentAdd(mHost, mRootView);
    TestRenderState(mHost, mRootView);
    TestTwoSurfaces(mHost, mViews);
    TestSiblingBindings(mHost, mViews);
    TestListResend(mHost, mViews);
    TestListRebuildAcrossSurfaces(mHost, mViews);
    TestListResendReleasesItems(mHost, mViews);
    TestSelfContainingComponent(mHost, mViews);
    TestSharedTemplate(mHost, mViews);
    TestInlineTemplateResend(mHost, mViews);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  RESULTS: " << gPassed << "/" << gTotal << " passed" << std::endl;
    std::cout << "========================================" << std::endl;

    // Quit() during InitSignal is swallowed (the main loop has not started yet),
    // so ask to leave on the first tick instead.
    mQuitTimer = Dali::Timer::New(1);
    mQuitTimer.TickSignal().Connect(this, &TestApp::OnQuitTick);
    mQuitTimer.Start();
    (void)app;
  }

  bool OnQuitTick()
  {
    mApplication.Quit();
    return false;
  }

  Dali::Application& mApplication;
  A2ui::A2uiHost     mHost;
  View               mRootView;
  std::map<std::string, View> mViews;
  Dali::Timer        mQuitTimer;
};

int DALI_EXPORT_API main(int argc, char** argv)
{
  (void)argv;

  Dali::WindowData windowData;
  Dali::Rect<int>  positionSize(0, 0, 720, 1080);
  windowData.SetPositionSize(positionSize);
  Dali::Application application = Dali::Application::New(&argc, &argv, "", false, windowData);

  // dali-ui refuses to build views until a UiConfig has been applied.
  Dali::Ui::UiConfig uiConfig = Dali::Ui::UiConfig::New();
  uiConfig.SetDpi(160);
  uiConfig.Apply();

  TestApp test(application);
  application.MainLoop();

  return gFailed == 0 ? 0 : 1;
}
