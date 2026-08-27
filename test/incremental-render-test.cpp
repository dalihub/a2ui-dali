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
    mHost.SetOnBeginRenderingSurface([this](const std::string&, View view) {
      mRootView = view;
    });

    TestIncrementalTextUpdate(mHost, mRootView);
    TestIncrementalComponentAdd(mHost, mRootView);
    TestRenderState(mHost, mRootView);

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
