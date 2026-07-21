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
 * End-to-end streaming/render test for a2ui-dali.
 *
 * The conformance test covers message parsing only. This one drives the REAL
 * A2uiHost + renderer and inspects the REAL DALi view tree, because the bugs
 * that matter live in the gap between them: a value that arrives in a later
 * `updateDataModel` must reach the already-rendered view.
 *
 * Each case is fed three ways, and all three must agree with the expected text:
 *   - Incremental : one JsonFeed() call per message (a live transport)
 *   - String      : one JsonFeed() call with the whole payload
 *   - File        : JsonFeedFile() (batched — renders once at the end)
 *
 * The File mode already worked before this test existed; the other two are the
 * streaming path, so the three-way comparison is what pins the regression.
 *
 * Needs a display (it builds actual DALi actors) — run it under Xvfb:
 *   tools/run-tests.sh
 *
 * Usage:
 *   ./a2ui-streaming-render-test [repo-root]     (default: "..", i.e. run from bin/)
 */

#include "renderer/a2ui-host.h"

#include <dali/public-api/adaptor-framework/application.h>
#include <dali/public-api/adaptor-framework/timer.h>
#include <dali/public-api/adaptor-framework/window-data.h>
#include <dali/public-api/math/rect.h>
#include <dali-ui-foundation/dali-ui-foundation.h>
#include <dali-ui-foundation/public-api/views/text-controls/label.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

std::string Join(const std::vector<std::string>& v)
{
  std::string out = "[";
  for(std::size_t i = 0; i < v.size(); ++i)
  {
    if(i) out += ", ";
    out += "\"" + v[i] + "\"";
  }
  return out + "]";
}

// Every Label in the rendered tree, in depth-first (visual) order. This is the
// assertion surface: what the user would actually read on screen.
void CollectLabels(View view, std::vector<std::string>& out)
{
  if(!view) return;
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

std::string ReadFile(const std::string& path)
{
  std::ifstream file(path);
  if(!file.is_open()) return std::string();
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

enum class FeedMode
{
  Incremental, // one JsonFeed() per message — a live transport
  String,      // one JsonFeed() with the whole payload
  File         // JsonFeedFile() — batched, renders once at the end
};

const char* ModeName(FeedMode mode)
{
  switch(mode)
  {
    case FeedMode::Incremental: return "incremental";
    case FeedMode::String:      return "string";
    default:                    return "file";
  }
}

// Feed `path` through a fresh host in `mode` and return the rendered label texts.
std::vector<std::string> Render(const std::string& path, FeedMode mode)
{
  A2ui::A2uiHost host;
  View           root;
  host.SetOnBeginRenderingSurface([&root](const std::string&, View view) { root = view; });

  if(mode == FeedMode::File)
  {
    host.JsonFeedFile(path);
  }
  else if(mode == FeedMode::String)
  {
    host.JsonFeed(ReadFile(path));
  }
  else
  {
    std::istringstream lines(ReadFile(path));
    std::string        line;
    while(std::getline(lines, line))
    {
      if(line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
      host.JsonFeed(line);
    }
  }

  std::vector<std::string> texts;
  CollectLabels(root, texts);
  return texts;
}

struct Case
{
  std::string              name;
  std::string              file;
  std::vector<std::string> expected;
};

// A case passes only when all three feed modes render exactly `expected`.
void RunCase(const std::string& e2eDir, const Case& c)
{
  for(FeedMode mode : {FeedMode::Incremental, FeedMode::String, FeedMode::File})
  {
    std::string label = c.name + " (" + ModeName(mode) + ")";
    try
    {
      std::vector<std::string> actual = Render(e2eDir + c.file, mode);
      ReportTest(label, actual == c.expected,
                 "expected " + Join(c.expected) + " but got " + Join(actual));
    }
    catch(const Dali::DaliException& e)
    {
      // condition/location can be null, so never feed them straight to operator+.
      auto safe = [](const char* s) { return s ? std::string(s) : std::string("(null)"); };
      ReportTest(label, false, "DaliException: " + safe(e.condition) + " at " + safe(e.location));
    }
    catch(const std::exception& e)
    {
      ReportTest(label, false, std::string("exception: ") + e.what());
    }
  }
}

// Streaming and batched feeds of the same payload must render identically. This
// is the cheap regression net over every payload the repo ships: it needs no
// hand-written expectations, so it keeps working as the samples evolve.
void RunParitySweep(const std::string& title, const std::string& dir)
{
  std::cout << "\n=== Parity: " << title << " (streaming feed == batched feed) ===" << std::endl;
  if(!std::filesystem::is_directory(dir))
  {
    ReportTest(title, false, "directory not found: " + dir);
    return;
  }

  std::vector<std::string> files;
  for(const auto& entry : std::filesystem::directory_iterator(dir))
  {
    std::string name = entry.path().filename().string();
    if(name.size() > 5 && name.substr(name.size() - 5) == ".json") files.push_back(name);
  }
  std::sort(files.begin(), files.end());

  for(const std::string& name : files)
  {
    std::vector<std::string> batched   = Render(dir + name, FeedMode::File);
    std::vector<std::string> streamed  = Render(dir + name, FeedMode::String);
    ReportTest(title + "/" + name, batched == streamed,
               "batched " + Join(batched) + " but streamed " + Join(streamed));
  }
}

void RunAllTests(const std::string& root)
{
  const std::string e2eDir = root + "/test/e2e/";

  const std::vector<Case> cases = {
    // A FunctionCall binding must be re-evaluated when the data it reads changes —
    // including paths that only appear inside a formatString "${...}" template.
    {"function-call binding updates", "function-call-binding.jsonl",
     {"Ada", "Tue", "high 80", "$1,234.50", "80/58"}},

    // Not just Text: an input's displayed value must go through the same binding on the
    // first paint and on every update, or the two disagree in opposite directions.
    // (Slider value, DateTimeInput label, its value, its empty error label.)
    {"function-call binding on inputs", "function-call-inputs.jsonl",
     {"1,234", "Date/Time", "12/16/2025, 9:30 AM", ""}},

    // children:{path, componentId} whose array arrives AFTER the components.
    {"template children appear when array arrives", "template-children.jsonl",
     {"Tue", "74deg", "Wed", "76deg", "Thu", "71deg"}},

    // A field of an already-rendered template item changes.
    {"template child item value updates", "template-children-item-update.jsonl",
     {"Tue", "74deg", "Wed", "99deg"}},

    // The bound array grows after the first render.
    {"template children follow array growth", "template-children-grow.jsonl",
     {"a", "b", "c"}},
  };

  std::cout << "\n=== Streaming data binding ===" << std::endl;
  for(const Case& c : cases)
  {
    RunCase(e2eDir, c);
  }

  RunParitySweep("samples", root + "/examples/samples/");
  RunParitySweep("gallery screens", root + "/examples/gallery-demo/screens/");
}

class TestApp : public Dali::ConnectionTracker
{
public:
  TestApp(Dali::Application& app, std::string root)
  : mApplication(app), mRoot(std::move(root))
  {
    mApplication.InitSignal().Connect(this, &TestApp::OnInit);
  }

private:
  void OnInit(Dali::Application app)
  {
    std::cout << "========================================" << std::endl;
    std::cout << "  a2ui-dali streaming render test" << std::endl;
    std::cout << "========================================" << std::endl;

    RunAllTests(mRoot);

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
    return false; // one-shot
  }

  Dali::Application& mApplication;
  std::string        mRoot;
  Dali::Timer        mQuitTimer;
};
} // namespace

int DALI_EXPORT_API main(int argc, char** argv)
{
  std::string root = (argc > 1) ? argv[1] : "..";

  Dali::WindowData windowData;
  Dali::Rect<int>  positionSize(0, 0, 720, 1080);
  windowData.SetPositionSize(positionSize);
  Dali::Application application = Dali::Application::New(&argc, &argv, "", false, windowData);

  // dali-ui refuses to build views until a UiConfig has been applied.
  Dali::Ui::UiConfig uiConfig = Dali::Ui::UiConfig::New();
  uiConfig.SetDpi(160);
  uiConfig.Apply();

  TestApp test(application, root);
  application.MainLoop();

  return gFailed == 0 ? 0 : 1;
}
