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
 * A2UI v0.9 Conformance Test for a2ui-dali renderer.
 *
 * Feeds official A2UI test case data through the C++ message processor
 * and verifies:
 * 1. All valid messages parse without error
 * 2. Surface/component/data models are correctly populated
 * 3. Component tree structure is valid
 *
 * Usage:
 *   ./conformance-test [test-data-dir]
 *
 * Default test-data-dir: ../test/
 */

#include "../src/core/a2ui-message-processor.h"
#include "../src/core/data-model.h"
#include "../src/core/surface-model.h"

#include <dali-ui-foundation/integration-api/builder/json-parser.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>

using namespace A2ui;
using Dali::Ui::Integration::TreeNode;

// ========================================================================
// Test Infrastructure
// ========================================================================

struct TestResult
{
  std::string name;
  bool passed;
  std::string error;
};

static int gTotal = 0;
static int gPassed = 0;
static int gFailed = 0;
static std::vector<TestResult> gResults;

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
  gResults.push_back({name, passed, error});
}

// ========================================================================
// Test 1: Parse valid JSONL messages
// ========================================================================

void TestValidMessages(const std::string& dataDir)
{
  std::cout << "\n=== Test: Valid Message Parsing ===" << std::endl;

  std::string filepath = dataDir + "conformance_valid_messages.jsonl";
  std::ifstream file(filepath);
  if(!file.is_open())
  {
    ReportTest("Load test data", false, "Cannot open " + filepath);
    return;
  }

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  std::string line;
  int lineNum = 0;
  while(std::getline(file, line))
  {
    lineNum++;
    if(line.empty()) continue;

    // Each message is independent — some are createSurface, some updateComponents, etc.
    // For updateComponents/updateDataModel, we need a surface to exist first.
    // Create a dummy surface if needed.
    if(line.find("updateComponents") != std::string::npos ||
       line.find("updateDataModel") != std::string::npos ||
       line.find("callFunction") != std::string::npos)
    {
      if(!surface.IsCreated())
      {
        // Pre-create a surface for messages that require one
        std::string createMsg = R"({"version":"v0.9","createSurface":{"surfaceId":"test_surface","catalogId":"basic"}})";
        processor.ProcessLine(createMsg, surface);
      }
    }

    bool ok = processor.ProcessLine(line, surface);
    std::string desc = "Line " + std::to_string(lineNum);

    // Extract message type for better test name
    if(line.find("createSurface") != std::string::npos) desc += " (createSurface)";
    else if(line.find("updateComponents") != std::string::npos) desc += " (updateComponents)";
    else if(line.find("updateDataModel") != std::string::npos) desc += " (updateDataModel)";
    else if(line.find("callFunction") != std::string::npos) desc += " (callFunction)";
    else if(line.find("deleteSurface") != std::string::npos) desc += " (deleteSurface)";

    if(ok)
    {
      ReportTest(desc, true);
    }
    else
    {
      ReportTest(desc, false, processor.GetLastError());
    }
  }
}

// ========================================================================
// Test 2: Contact Form Example (full JSONL flow)
// ========================================================================

void TestContactFormExample(const std::string& dataDir)
{
  std::cout << "\n=== Test: Contact Form Example (End-to-End) ===" << std::endl;

  std::string filepath = dataDir + "contact_form_example.jsonl";
  std::ifstream file(filepath);
  if(!file.is_open())
  {
    ReportTest("Load contact_form_example.jsonl", false, "Cannot open " + filepath);
    return;
  }

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  // Process line by line to verify intermediate states
  std::ifstream lineFile(filepath);
  std::string jsonlLine;
  int lineCount = 0;
  bool allOk = true;

  while(std::getline(lineFile, jsonlLine))
  {
    lineCount++;
    if(jsonlLine.empty()) continue;

    bool ok = processor.ProcessLine(jsonlLine, surface);
    if(!ok)
    {
      ReportTest("JSONL line " + std::to_string(lineCount), false, processor.GetLastError());
      allOk = false;
    }

    // After line 1 (createSurface): surface should exist
    if(lineCount == 1)
    {
      ReportTest("After createSurface: surface exists", surface.IsCreated());
    }
    // After line 2 (updateComponents): should have components with root
    else if(lineCount == 2)
    {
      int compCount = surface.GetComponentCount();
      ReportTest("After updateComponents: has components", compCount > 0,
                 "count=" + std::to_string(compCount));

      const auto* root = surface.GetComponentsModel().GetRoot();
      ReportTest("After updateComponents: root exists", root != nullptr);

      if(root)
      {
        ReportTest("Root is Card", root->type == "Card",
                   "got: " + root->type);
      }
    }
    // After line 3 (updateDataModel): data should be set
    else if(lineCount == 3)
    {
      ReportTest("After updateDataModel: processed", ok);
    }
    // After line 4 (deleteSurface): surface should be deleted
    else if(lineCount == 4)
    {
      ReportTest("After deleteSurface: surface deleted", !surface.IsCreated());
    }
  }

  ReportTest("All " + std::to_string(lineCount) + " JSONL lines parsed", allOk);
}

// ========================================================================
// Test 3: Component Type Coverage
// ========================================================================

void TestComponentTypeCoverage(const std::string& dataDir)
{
  std::cout << "\n=== Test: Component Type Coverage ===" << std::endl;

  // List of all v0.9 basic catalog components
  std::vector<std::string> allComponents = {
    "Text", "Image", "Icon", "Video", "AudioPlayer",
    "Row", "Column", "List", "Card", "Tabs", "Modal", "Divider",
    "Button", "TextField", "CheckBox", "ChoicePicker", "Slider", "DateTimeInput"
  };

  // Build a test message with each component type
  A2uiMessageProcessor processor;

  for(const auto& comp : allComponents)
  {
    SurfaceModel surface;

    // Create surface
    std::string createMsg = R"({"version":"v0.9","createSurface":{"surfaceId":"test","catalogId":"basic"}})";
    processor.ProcessLine(createMsg, surface);

    // Build minimal updateComponents with this component as root
    std::string updateMsg;
    if(comp == "Row" || comp == "Column" || comp == "List")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":")" +
                  comp + R"(","children":[]}]}})";
    }
    else if(comp == "Card" || comp == "Button" || comp == "Modal")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"child","component":"Text","text":"test"},{"id":"root","component":")" +
                  comp + R"(","child":"child"}]}})";
    }
    else if(comp == "Tabs")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"tab_content","component":"Text","text":"tab"},{"id":"root","component":"Tabs","tabs":[{"title":"Tab1","child":"tab_content"}]}]}})";
    }
    else if(comp == "TextField")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"TextField","label":"Name","value":{"path":"/name"}}]}})";
    }
    else if(comp == "CheckBox")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"CheckBox","label":"Accept","value":{"path":"/accept"}}]}})";
    }
    else if(comp == "Slider")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"Slider","label":"Volume","value":{"path":"/vol"},"min":0,"max":100}]}})";
    }
    else if(comp == "ChoicePicker")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"ChoicePicker","label":"Color","value":{"path":"/color"},"options":[{"label":"Red","value":"red"}]}]}})";
    }
    else if(comp == "DateTimeInput")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"DateTimeInput","label":"Date","value":{"path":"/date"}}]}})";
    }
    else if(comp == "Image")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"Image","url":"https://example.com/img.png"}]}})";
    }
    else if(comp == "Icon")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"Icon","name":"home"}]}})";
    }
    else if(comp == "Video")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"Video","url":"https://example.com/vid.mp4"}]}})";
    }
    else if(comp == "AudioPlayer")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"AudioPlayer","url":"https://example.com/audio.mp3"}]}})";
    }
    else if(comp == "Divider")
    {
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":"Divider"}]}})";
    }
    else
    {
      // Text and others
      updateMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[{"id":"root","component":")" +
                  comp + R"(","text":"Hello"}]}})";
    }

    bool ok = processor.ProcessLine(updateMsg, surface);
    const auto* root = surface.GetComponentsModel().GetRoot();

    if(ok && root)
    {
      ReportTest(comp + " — parse & root found", true);
    }
    else if(ok)
    {
      ReportTest(comp + " — parsed but no root", false, processor.GetLastError());
    }
    else
    {
      ReportTest(comp + " — parse failed", false, processor.GetLastError());
    }
  }
}

// ========================================================================
// Test 4: Data Binding
// ========================================================================

void TestDataBinding(const std::string& dataDir)
{
  std::cout << "\n=== Test: Data Binding ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  // Create surface
  processor.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"binding-test","catalogId":"basic"}})",
    surface);

  // Components with data binding
  processor.ProcessLine(
    R"({"version":"v0.9","updateComponents":{"surfaceId":"binding-test","components":[
      {"id":"title","component":"Text","text":{"path":"/name"},"variant":"h1"},
      {"id":"desc","component":"Text","text":{"path":"/description"}},
      {"id":"root","component":"Column","children":["title","desc"]}
    ]}})",
    surface);

  ReportTest("Components with binding paths", surface.GetComponentCount() == 3,
             "count=" + std::to_string(surface.GetComponentCount()));

  // Update data model
  bool dmOk = processor.ProcessLine(
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"binding-test","path":"/","value":{"name":"Hello A2UI","description":"Conformance test"}}})",
    surface);

  ReportTest("UpdateDataModel processed", dmOk,
             dmOk ? "" : processor.GetLastError());
}

// ========================================================================
// Test 5: Theme Validation
// ========================================================================

void TestTheme(const std::string& dataDir)
{
  std::cout << "\n=== Test: Theme in CreateSurface ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  bool ok = processor.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"themed","catalogId":"basic","theme":{"primaryColor":"#FF5733","agentDisplayName":"Test Agent"}}})",
    surface);

  ReportTest("CreateSurface with theme", ok && surface.IsCreated(),
             ok ? "" : processor.GetLastError());
}

// ========================================================================
// Test 6: Delete Surface
// ========================================================================

void TestDeleteSurface(const std::string& dataDir)
{
  std::cout << "\n=== Test: Delete Surface ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  processor.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"to-delete","catalogId":"basic"}})",
    surface);

  ReportTest("Surface exists before delete", surface.IsCreated());

  bool ok = processor.ProcessLine(
    R"({"version":"v0.9","deleteSurface":{"surfaceId":"to-delete"}})",
    surface);

  ReportTest("DeleteSurface processed", ok,
             ok ? "" : processor.GetLastError());
}

// ========================================================================
// Test 7: DataModel — writes into arrays
//
// A list is addressed by index, and an update that cannot name a real slot must leave the
// list alone. Getting this wrong is invisible when the value is read straight back (an
// object with a "1" key answers /f/1 just fine) and only surfaces on the next render, when
// a data-driven child list no longer finds an ARRAY.
// ========================================================================

void TestDataModelArrayWrites()
{
  std::cout << "\n=== Test: DataModel array writes ===" << std::endl;

  auto fresh = [](const char* json) {
    auto model = std::make_unique<DataModel>();
    model->SetData("/", json);
    return model;
  };
  auto isArray = [](DataModel& m, const char* path) {
    const auto* node = m.ResolvePath(path);
    return node && node->GetType() == TreeNode::ARRAY;
  };

  {
    auto m = fresh(R"({"f":[{"t":"a"},{"t":"b"}]})");
    ReportTest("array item write keeps the array", m->SetData("/f/1/t", "\"z\"") &&
               isArray(*m, "/f") && m->GetString("/f/1/t") == "z" &&
               m->GetString("/f/0/t") == "a");
  }
  {
    auto m = fresh(R"({"f":[{"t":"a"}]})");
    // index == length is the JSON Pointer append slot — the ordinary way a stream grows a list
    ReportTest("append at index == length", m->SetData("/f/1/t", "\"b\"") &&
               isArray(*m, "/f") && m->GetString("/f/0/t") == "a" &&
               m->GetString("/f/1/t") == "b");
  }
  {
    auto m = fresh(R"({"f":[{"t":"a"}]})");
    ReportTest("append via the \"-\" token", m->SetData("/f/-/t", "\"b\"") &&
               isArray(*m, "/f") && m->GetString("/f/1/t") == "b");
  }
  {
    auto m = fresh(R"({"f":[]})");
    ReportTest("first item into an empty array", m->SetData("/f/0/t", "\"a\"") &&
               isArray(*m, "/f") && m->GetString("/f/0/t") == "a");
  }
  {
    auto m = fresh(R"({"f":[{"t":"a"}]})");
    bool rejected = !m->SetData("/f/7/t", "\"ghost\"");
    ReportTest("index past the append slot is rejected",
               rejected && isArray(*m, "/f") && m->GetString("/f/1/t").empty() &&
               m->GetString("/f/0/t") == "a");
  }
  {
    auto m = fresh(R"({"f":[{"t":"a"}]})");
    bool rejected = !m->SetData("/f/name/t", "\"junk\"");
    ReportTest("field name where an index belongs is rejected",
               rejected && isArray(*m, "/f") && m->GetString("/f/0/t") == "a");
  }
  {
    auto m = fresh(R"([{"n":"a"},{"n":"b"}])");
    ReportTest("root-level array updates in place", m->SetData("/1/n", "\"z\"") &&
               isArray(*m, "/") && m->GetString("/0/n") == "a" && m->GetString("/1/n") == "z");
  }
  {
    auto m = fresh(R"({"f":[{"t":"a"},{"t":"b"}]})");
    // Reporting an out-of-range index as a hit made "/f/2" resolve to the array itself.
    ReportTest("out-of-range index resolves to nothing", m->ResolvePath("/f/2") == nullptr);
  }
  {
    auto m = fresh(R"({"a":[{"b":[{"c":1},{"c":2}]}]})");
    ReportTest("nested arrays", m->SetData("/a/0/b/1/c", "9") &&
               isArray(*m, "/a") && isArray(*m, "/a/0/b") &&
               m->GetString("/a/0/b/0/c") == "1" && m->GetString("/a/0/b/1/c") == "9");
  }
}

// ========================================================================
// Test 8: DataModel — observer bookkeeping
//
// The renderer rebuilds parts of the view tree from inside a notification, so registering,
// retiring and clearing observers all have to behave while a notification is in flight.
// ========================================================================

void TestDataModelObservers()
{
  std::cout << "\n=== Test: DataModel observers ===" << std::endl;

  {
    DataModel model;
    model.SetData("/", R"({"a":1,"b":2})");
    uint32_t first  = model.Watch("/a", [](const std::string&, const std::string&) {});
    uint32_t second = model.Watch("/b", [](const std::string&, const std::string&) {});
    model.UnwatchAll({first, second});
    ReportTest("UnwatchAll removes the listed observers", model.ObserverCount() == 0);
  }
  {
    DataModel model;
    model.SetData("/", R"({"a":1})");
    int fired = 0;
    model.Watch("/a", [&fired](const std::string&, const std::string&) { fired++; });
    model.SetData("/a", "2");
    ReportTest("a watch fires on its own path", fired == 1);
  }
  {
    // Registering during a notification must not lose the new observer.
    DataModel model;
    model.SetData("/", R"({"a":1})");
    bool added = false;
    model.Watch("/a", [&model, &added](const std::string&, const std::string&) {
      if(!added)
      {
        added = true;
        model.Watch("/a", [](const std::string&, const std::string&) {});
      }
    });
    model.SetData("/a", "2");
    ReportTest("a watch registered during a notification survives", model.ObserverCount() == 2);
  }
  {
    // Retiring during a notification must also drop registrations queued in the same pass —
    // pending removals are applied first, so a deferred remove alone would miss them.
    DataModel model;
    model.SetData("/", R"({"a":1})");
    std::vector<uint32_t> spawned;
    model.Watch("/a", [&model, &spawned](const std::string&, const std::string&) {
      if(!spawned.empty()) return;
      spawned.push_back(model.Watch("/a", [](const std::string&, const std::string&) {}));
      model.UnwatchAll(spawned);
    });
    model.SetData("/a", "2");
    ReportTest("UnwatchAll during a notification drops queued registrations",
               model.ObserverCount() == 1);
  }
  {
    // A callback may trigger a full re-render, which clears every observer. Clearing the
    // vector mid-loop would destroy the callback that is running AND silently skip the
    // observers after it, so the clear is deferred to the end of the pass: the rest of this
    // notification still runs, and only then does everything go.
    DataModel model;
    model.SetData("/", R"({"a":1})");
    int later = 0;
    model.Watch("/a", [&model](const std::string&, const std::string&) {
      model.ClearObservers();
    });
    model.Watch("/a", [&later](const std::string&, const std::string&) { later++; });
    model.SetData("/a", "2");
    ReportTest("ClearObservers from inside a notification defers to the end of the pass",
               later == 1 && model.ObserverCount() == 0,
               "later fired " + std::to_string(later) + " times, " +
               std::to_string(model.ObserverCount()) + " observers left");
  }
}

// ========================================================================
// Main
// ========================================================================

int main(int argc, char** argv)
{
  std::string dataDir = "../test/";
  if(argc > 1)
  {
    dataDir = argv[1];
    if(dataDir.back() != '/') dataDir += '/';
  }

  std::cout << "========================================" << std::endl;
  std::cout << "  A2UI v0.9 Conformance Test" << std::endl;
  std::cout << "  a2ui-dali renderer" << std::endl;
  std::cout << "  Test data: " << dataDir << std::endl;
  std::cout << "========================================" << std::endl;

  TestValidMessages(dataDir);
  TestContactFormExample(dataDir);
  TestComponentTypeCoverage(dataDir);
  TestDataBinding(dataDir);
  TestTheme(dataDir);
  TestDeleteSurface(dataDir);
  TestDataModelArrayWrites();
  TestDataModelObservers();

  // Summary
  std::cout << "\n========================================" << std::endl;
  std::cout << "  RESULTS: " << gPassed << "/" << gTotal << " passed";
  if(gFailed > 0)
  {
    std::cout << " (" << gFailed << " FAILED)";
  }
  std::cout << std::endl;

  if(gFailed > 0)
  {
    std::cout << "\n  Failed tests:" << std::endl;
    for(const auto& r : gResults)
    {
      if(!r.passed)
      {
        std::cout << "    - " << r.name << ": " << r.error << std::endl;
      }
    }
  }

  std::cout << "========================================" << std::endl;

  return gFailed > 0 ? 1 : 0;
}
