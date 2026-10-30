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
#include <dali-ui-foundation/public-api/views/image/image-view.h>
#include <dali-ui-foundation/public-api/views/text-controls/label.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Dali::Ui::ImageView;
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

// Everything the user can actually read off the screen, in depth-first (visual) order:
// label text, and image sources as "img:<basename>" (an Image url or an Icon glyph — both
// render as an ImageView, so this is how a bound image is asserted).
void CollectLabels(View view, std::vector<std::string>& out)
{
  if(!view) return;
  // A hidden view is not on screen — a validation error that was resolved is hidden, not
  // blanked, and asserting its stale text would be asserting something nobody can read.
  if(!view.GetProperty<bool>(Dali::Actor::Property::VISIBLE)) return;
  Label label = Label::DownCast(view);
  if(label)
  {
    out.push_back(std::string(label.GetText().CStr()));
  }
  ImageView image = ImageView::DownCast(view);
  if(image)
  {
    std::string url = image.GetResourceUrl().CStr();
    std::size_t slash = url.find_last_of('/');
    out.push_back("img:" + (slash == std::string::npos ? url : url.substr(slash + 1)));
  }
  for(uint32_t i = 0; i < view.GetChildViewCount(); ++i)
  {
    CollectLabels(view.GetChildViewAt(i), out);
  }
}

// Every layout decision the renderer made FROM the text — the width each view asked for, plus
// a label's horizontal alignment — tagged with what the view shows, in the same depth-first
// order as CollectLabels.
//
// Text alone cannot catch these: a button whose label arrived late renders the right string
// inside a box measured from the empty string it had at first paint, and a streamed paragraph
// is centred because "" always fits on one line. Both pass the text assertion while the screen
// is visibly wrong.
//
// The REQUESTED width is the right thing to read, not the arranged one: this test never runs a
// frame, so nothing has been laid out yet and GetSize() is still 0 everywhere. The requested
// width is the decision the renderer made, and re-measuring is exactly what has to update it.
void CollectGeometry(View view, std::vector<std::string>& out)
{
  if(!view) return;
  if(!view.GetProperty<bool>(Dali::Actor::Property::VISIBLE)) return;
  std::string tag = "view";
  std::string align;
  if(Label label = Label::DownCast(view))
  {
    tag = std::string(label.GetText().CStr());
    if(tag.empty()) tag = "(empty)";
    switch(label.GetHorizontalTextAlignment())
    {
      case Dali::Ui::Text::Alignment::CENTER: align = "/center"; break;
      case Dali::Ui::Text::Alignment::END:    align = "/end";    break;
      default:                                align = "/start";  break;
    }
  }
  else if(ImageView image = ImageView::DownCast(view))
  {
    std::string url = image.GetResourceUrl().CStr();
    std::size_t slash = url.find_last_of('/');
    tag = "img:" + (slash == std::string::npos ? url : url.substr(slash + 1));
  }
  // Round: a width is a device-pixel quantity, and comparing raw floats would fail on
  // arithmetic noise rather than on a real layout difference.
  out.push_back(tag + "=" + std::to_string(static_cast<long>(view.GetRequestedWidth() + 0.5f)) + align);
  for(uint32_t i = 0; i < view.GetChildViewCount(); ++i)
  {
    CollectGeometry(view.GetChildViewAt(i), out);
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

std::string gImageDir; // repo res/, so Image urls and Icon names resolve to real files

// Feed `path` through a fresh host in `mode` and return what the screen shows.
// `observersOut`, if given, receives the number of data-model observers still registered
// across all surfaces once the feed is done.
std::vector<std::string> Render(const std::string& path, FeedMode mode,
                                size_t* observersOut = nullptr,
                                size_t* tapDetectorsOut = nullptr,
                                std::vector<std::string>* geometryOut = nullptr)
{
  A2ui::A2uiHost host;
  View           root;
  host.GetRenderer().SetImageDir(gImageDir);
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

  if(observersOut)
  {
    *observersOut = 0;
    for(const std::string& id : host.GetSurfaces().GetSurfaceIds())
    {
      if(A2ui::SurfaceModel* surface = host.GetSurfaces().GetSurface(id))
      {
        *observersOut += surface->GetDataModel().ObserverCount();
      }
    }
  }

  if(tapDetectorsOut) *tapDetectorsOut = host.GetRenderer().GetTapDetectorCount();

  if(geometryOut) CollectGeometry(root, *geometryOut);

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

// A list that rebuilt itself while streaming must end up with the same number of live
// watches as a fresh render of the same final data. More means a rebuild left observers
// behind on views that are no longer on screen — they keep firing, and they accumulate.
void RunObserverParity(const std::string& e2eDir, const Case& c)
{
  size_t streamedWatches = 0, batchedWatches = 0, streamedTaps = 0, batchedTaps = 0;
  Render(e2eDir + c.file, FeedMode::Incremental, &streamedWatches, &streamedTaps);
  Render(e2eDir + c.file, FeedMode::File, &batchedWatches, &batchedTaps);
  ReportTest(c.name + " (no leaked watches)", streamedWatches == batchedWatches,
             "streaming left " + std::to_string(streamedWatches) + " observers, a fresh render "
             "of the same data has " + std::to_string(batchedWatches));
  ReportTest(c.name + " (no leaked tap targets)", streamedTaps == batchedTaps,
             "streaming left " + std::to_string(streamedTaps) + " tap detectors, a fresh render "
             "of the same data has " + std::to_string(batchedTaps));
}

// Same text is not the same screen. A box measured from the value a view held at first paint
// stays that size after the real value streams in, so the label reads correctly and still
// clips. Batched (`JsonFeedFile`) renders once with the data already in place, so its geometry
// is by construction what the payload asks for — the streamed feed must land on the same
// widths. No hand-written numbers, so this keeps holding as the metrics evolve.
void RunGeometryParity(const std::string& e2eDir, const Case& c)
{
  std::vector<std::string> streamed, batched;
  Render(e2eDir + c.file, FeedMode::Incremental, nullptr, nullptr, &streamed);
  Render(e2eDir + c.file, FeedMode::File, nullptr, nullptr, &batched);
  ReportTest(c.name + " (same widths as a batched feed)", streamed == batched,
             "streaming laid out " + Join(streamed) + " but a fresh render of the same data "
             "laid out " + Join(batched));
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
    std::vector<std::string> batchedGeom, streamedGeom;
    std::vector<std::string> batched  = Render(dir + name, FeedMode::File,   nullptr, nullptr, &batchedGeom);
    std::vector<std::string> streamed = Render(dir + name, FeedMode::String, nullptr, nullptr, &streamedGeom);
    ReportTest(title + "/" + name, batched == streamed,
               "batched " + Join(batched) + " but streamed " + Join(streamed));
    ReportTest(title + "/" + name + " (layout)", batchedGeom == streamedGeom,
               "batched laid out " + Join(batchedGeom) + " but streamed laid out " + Join(streamedGeom));
  }
}

void RunAllTests(const std::string& root)
{
  const std::string e2eDir = root + "/test/e2e/";
  gImageDir                = root + "/res/";

  const std::vector<Case> cases = {
    // A FunctionCall binding must be re-evaluated when the data it reads changes —
    // including paths that only appear inside a formatString "${...}" template.
    {"function-call binding updates", "function-call-binding.jsonl",
     {"Ada", "Tue", "high 80", "$1,234.50", "80/58"}},

    // Not just Text: an input's displayed value must go through the same binding on the
    // first paint and on every update, or the two disagree in opposite directions.
    // (Slider value, DateTimeInput label, its value, its calendar glyph.)
    {"function-call binding on inputs", "function-call-inputs.jsonl",
     {"1,234", "Date/Time", "12/16/2025, 9:30 AM", "img:calendarToday.png"}},

    // children:{path, componentId} whose array arrives AFTER the components.
    {"template children appear when array arrives", "template-children.jsonl",
     {"Tue", "74deg", "Wed", "76deg", "Thu", "71deg"}},

    // A field of an already-rendered template item changes.
    {"template child item value updates", "template-children-item-update.jsonl",
     {"Tue", "74deg", "Wed", "99deg"}},

    // The bound array grows after the first render.
    {"template children follow array growth", "template-children-grow.jsonl",
     {"a", "b", "c"}},

    // Appending by writing the fields of the next index — the ordinary way a stream grows
    // a list. The index equals the current length, which is the JSON Pointer append slot.
    {"template children follow a field-wise append", "template-children-append.jsonl",
     {"Mon", "1deg", "Tue", "2deg"}},

    // Same, but the list starts out empty.
    {"template children fill from an empty array", "template-children-from-empty.jsonl",
     {"Mon", "1deg"}},

    // A write the array cannot accept (index past the append slot, or a field name where
    // an index belongs) must leave it alone rather than invent a row.
    {"out-of-range array write leaves the list intact", "template-children-out-of-range.jsonl",
     {"Mon", "1deg"}},

    // The data model root is itself an array.
    {"array-root item updates in place", "array-root-item-update.jsonl",
     {"a", "z"}},

    // Bound images: an Image url composed by formatString, and an Icon name. Neither is
    // text, so they are asserted through the resource the view ends up showing.
    {"bound image and icon follow the data", "image-and-icon-binding.jsonl",
     {"img:092f80161d74.jpg", "img:close.png"}},

    // A bound AudioPlayer caption (which used to be skipped entirely when empty at first
    // paint), and a validation rule whose condition reads a path OTHER than the field's own
    // value — the error must clear when that other path is filled in.
    {"bound caption and cross-field validation", "checks-and-audio.jsonl",
     {"The Future of AI", "img:play.png", "Confirm"}},

    // A list of Buttons that grows twice: each rebuild attaches fresh tap detectors, and
    // the old ones must not pile up holding views that are no longer on screen.
    {"list of buttons grows", "list-with-buttons.jsonl", {"one", "two", "three"}},

    // A list inside a list: the inner one rebuilds, then the outer one rebuilds over it.
    {"nested template lists", "nested-template-children.jsonl",
     {"a", "b", "c"}},

    // A Button sizes itself to its label. When the label is bound, the label is empty at
    // first paint, so the button must re-measure when the value streams in — otherwise the
    // text renders correctly (which is all the assertions above would see) inside a box cut
    // for the empty string. Four labels, because the button has two size branches and both
    // have to survive: a long word run, a short ASCII word, a real single glyph (stays a
    // square), and a literal label (must not move at all).
    {"button re-measures when its label arrives", "button-label-arrives-later.jsonl",
     {"Purchase License", "OK", "\xe2\x96\xb6", "Purchase License"}},
  };

  std::cout << "\n=== Streaming data binding ===" << std::endl;
  for(const Case& c : cases)
  {
    RunCase(e2eDir, c);
  }

  std::cout << "\n=== Layout re-measurement ===" << std::endl;
  for(const Case& c : cases)
  {
    RunGeometryParity(e2eDir, c);
  }

  std::cout << "\n=== Watch bookkeeping across rebuilds ===" << std::endl;
  for(const Case& c : cases)
  {
    RunObserverParity(e2eDir, c);
  }

  RunParitySweep("samples", root + "/examples/samples/");
  RunParitySweep("gallery screens", root + "/examples/gallery-demo/screens/");
}


// === Stagger replay guard (staged, timer-driven) ===
//
// Regression coverage for the two stagger bugs seen in the movie demo:
//   1. partial-render state leaked out of RenderComponent's fresh-subtree
//      branch, so ancestor containers re-staggered children that were only
//      being REUSED (the movie-detail block floated up a second time);
//   2. the first fix attempt suppressed the leak so thoroughly that NO
//      entrance animation ever played on the streaming path.
//
// Stage 1 feeds the movie-detail batch and arms a timer so every entrance
// animation completes (BAKE restores opacity to 1). Stage 2 feeds the
// recommendations batch and asserts IMMEDIATELY — before the new 100ms
// stagger timers can fire — that:
//   (a) movie-detail labels and ALL their ancestors are fully opaque
//       (no replay on already-visible content);
//   (b) the recommendations title has some ancestor at opacity 0 (the NEW
//       section's entrance animation IS playing);
//   (c) the recommendations content actually rendered.
struct StaggerReplayFixture
{
  A2ui::A2uiHost host;
  View           rootView;
  Dali::Timer    pumpTimer;
  FeedMode       mode = FeedMode::Incremental;
  std::string    e2eDir;
};

View FindLabelByText(View view, const std::string& text)
{
  if(!view) return View();
  if(!view.GetProperty<bool>(Dali::Actor::Property::VISIBLE)) return View();
  Label label = Label::DownCast(view);
  if(label && std::string(label.GetText().CStr()) == text) return label;
  for(uint32_t i = 0; i < view.GetChildViewCount(); ++i)
  {
    View found = FindLabelByText(view.GetChildViewAt(i), text);
    if(found) return found;
  }
  return View();
}

// Opacities of `leaf` itself and every ancestor up to (excluding) `root`.
void CollectAncestorOpacities(View leaf, View root, std::vector<float>& out)
{
  for(View v = leaf; v && v != root;)
  {
    out.push_back(v.GetProperty<float>(Dali::Actor::Property::OPACITY));
    Dali::Actor parent = v.GetParent();
    v = parent ? View::DownCast(parent) : View();
  }
}

void FeedFixture(StaggerReplayFixture& fx, const std::string& file)
{
  const std::string path = fx.e2eDir + file;
  if(fx.mode == FeedMode::String)
  {
    fx.host.JsonFeed(ReadFile(path));
    return;
  }
  std::istringstream lines(ReadFile(path));
  std::string        line;
  while(std::getline(lines, line))
  {
    if(line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
    fx.host.JsonFeed(line);
  }
}

void RunStaggerStage2(StaggerReplayFixture& fx)
{
  const std::string mode = ModeName(fx.mode);
  try
  {
    FeedFixture(fx, "recommendations-batch.jsonl");

    // (a) no re-float of already-visible movie-detail content
    for(const char* text : {"The Shawshank Redemption", "Director: Frank Darabont"})
    {
      View leaf = FindLabelByText(fx.rootView, text);
      if(!leaf)
      {
        ReportTest(std::string("movie detail '") + text + "' present (" + mode + ")", false,
                   "label not found in the view tree");
        continue;
      }
      std::vector<float> ops;
      CollectAncestorOpacities(leaf, fx.rootView, ops);
      auto  minIt      = std::min_element(ops.begin(), ops.end());
      float minOpacity = (minIt != ops.end()) ? *minIt : 1.0f;
      ReportTest(std::string("no re-float of '") + text + "' (" + mode + ")",
                 minOpacity >= 0.999f,
                 "some ancestor has opacity " + std::to_string(minOpacity) +
                   " — the upward float replayed on already-visible content");
    }

    // (b) the NEW recommendations section's entrance animation IS playing
    View recsLeaf = FindLabelByText(fx.rootView, "You Might Also Like");
    if(!recsLeaf)
    {
      ReportTest("recommendations title present (" + mode + ")", false,
                 "'You Might Also Like' not found in the view tree");
    }
    else
    {
      std::vector<float> ops;
      CollectAncestorOpacities(recsLeaf, fx.rootView, ops);
      bool anyZero = std::any_of(ops.begin(), ops.end(), [](float o) { return o < 0.001f; });
      ReportTest("recommendations entrance animation plays (" + mode + ")", anyZero,
                 "no view at opacity 0 — the new section's entrance float never started");
    }

    // (c) the recommendations content actually rendered
    View card = FindLabelByText(fx.rootView, "Cool Hand Luke");
    ReportTest("recommendations cards rendered (" + mode + ")", !!card,
               "'Cool Hand Luke' card not found in the view tree");
  }
  catch(const Dali::DaliException& e)
  {
    auto safe = [](const char* str) { return str ? std::string(str) : std::string("(null)"); };
    ReportTest("stagger replay guard (" + mode + ")", false,
               "DaliException: " + safe(e.condition) + " at " + safe(e.location));
  }
  catch(const std::exception& e)
  {
    ReportTest("stagger replay guard (" + mode + ")", false,
               std::string("exception: ") + e.what());
  }
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


    // The stagger-replay guard needs the main loop between its two stages
    // (batch-1 entrance animations must run to completion before batch 2 is
    // fed), so it runs as a timer-driven fixture AFTER the synchronous suites.
    mFixtureModes = {FeedMode::Incremental, FeedMode::String};
    StartNextStaggerFixture();
    (void)app;
  }


  void StartNextStaggerFixture()
  {
    if(mFixtureModeIndex >= mFixtureModes.size())
    {
      FinishAll();
      return;
    }

    std::cout << "\n=== Stagger replay guard (" << ModeName(mFixtureModes[mFixtureModeIndex])
              << ") ===" << std::endl;

    mFixture         = std::make_unique<StaggerReplayFixture>();
    mFixture->mode   = mFixtureModes[mFixtureModeIndex];
    mFixture->e2eDir = mRoot + "/test/e2e/";
    mFixture->host.GetRenderer().SetImageDir(gImageDir);

    // The fixture's root view MUST be added to the window: DALi animations
    // only progress for on-stage actors, and this test's assertions depend
    // on entrance animations running to completion (BAKE -> opacity 1).
    mFixture->host.SetOnBeginRenderingSurface(
      [this](const std::string&, View view) {
        mFixture->rootView = view;
        mApplication.GetWindow().Add(view);
      });

    try
    {
      FeedFixture(*mFixture, "movie-detail-batch.jsonl");
    }
    catch(const std::exception& e)
    {
      ReportTest(std::string("stagger fixture batch-1 (") + ModeName(mFixture->mode) + ")",
                 false, std::string("exception: ") + e.what());
      AdvanceStaggerFixture();
      return;
    }

    // Let batch-1's entrance animations run to completion (BAKE restores
    // opacity 1) — a batch-2 replay is then detectable as opacity 0.
    mFixture->pumpTimer = Dali::Timer::New(1800);
    mFixture->pumpTimer.TickSignal().Connect(this, &TestApp::OnStaggerStage2);
    mFixture->pumpTimer.Start();

  }

  bool OnStaggerStage2()
  {
    RunStaggerStage2(*mFixture);
    AdvanceStaggerFixture();
    return false; // one-shot
  }

  void AdvanceStaggerFixture()
  {

    if(mFixture && mFixture->rootView)
    {
      mApplication.GetWindow().Remove(mFixture->rootView);
    }
    mFixture.reset();
    ++mFixtureModeIndex;
    StartNextStaggerFixture();
  }

  void FinishAll()
  {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  RESULTS: " << gPassed << "/" << gTotal << " passed" << std::endl;
    std::cout << "========================================" << std::endl;

    // Quit() during InitSignal is swallowed (the main loop has not started yet),
    // so ask to leave on the first tick instead.
    mQuitTimer = Dali::Timer::New(1);
    mQuitTimer.TickSignal().Connect(this, &TestApp::OnQuitTick);
    mQuitTimer.Start();
  }

  bool OnQuitTick()
  {
    mApplication.Quit();
    return false; // one-shot
  }

  Dali::Application& mApplication;
  std::string        mRoot;
  Dali::Timer        mQuitTimer;

  std::vector<FeedMode>                 mFixtureModes;
  std::size_t                           mFixtureModeIndex = 0;
  std::unique_ptr<StaggerReplayFixture> mFixture;
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
