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
#include "../src/core/action-dispatcher.h"
#include "../src/core/a2ui-protocol.h"
#include "../src/core/surface-group-model.h"
#include "../src/core/expression-parser.h"

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

    // This file is a CATALOGUE of individually-valid messages, not one coherent session:
    // it carries several createSurface lines for the same surfaceId. Replaying them
    // against one session would (correctly) trip the duplicate-surfaceId rule, so each
    // createSurface here begins a fresh session — the test asks "does this message
    // parse?", and surface uniqueness is covered by its own test below.
    if(line.find("createSurface") != std::string::npos)
    {
      processor.Reset();
    }

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

  for(const auto& comp : allComponents)
  {
    // A fresh processor per component: each check is its own client session, so reusing
    // the same surfaceId across them is not a duplicate-createSurface stream error.
    A2uiMessageProcessor processor;
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
// Test 9: Renderer-to-agent `action` envelope conformance
//
// The v0.9/v0.9.1/v1.0 renderer_to_agent schema names this message `action`
// (`userAction` is v0.8 only) and requires
// name / surfaceId / sourceComponentId / timestamp / context, plus `version`.
// ========================================================================

void DispatchFixtureAction(const char* componentActionJson,
                           std::string& emittedOut)
{
  Dali::Ui::Integration::JsonParser parser = Dali::Ui::Integration::JsonParser::New();
  if(!parser.Parse(componentActionJson)) return;

  const Dali::Ui::Integration::TreeNode* root = parser.GetRoot();
  if(!root) return;
  const Dali::Ui::Integration::TreeNode* actionNode = root->Find("action");
  if(!actionNode) return;

  SurfaceModel surface;
  DataContext  ctx(surface.GetDataModel());

  ActionDispatcher dispatcher;
  dispatcher.SetSurfaceId("surface-1");
  dispatcher.SetSendCallback([&emittedOut](const std::string& json) { emittedOut = json; });
  dispatcher.Dispatch(*actionNode, "submit_btn", ctx);
}

void TestActionEnvelopeConformance()
{
  std::cout << "\n=== Test: Renderer-to-Agent `action` Envelope ===" << std::endl;

  std::string emitted;
  DispatchFixtureAction(
    R"({"action":{"event":{"name":"submit_form","context":{"note":"hello"}}}})",
    emitted);

  ReportTest("action dispatched", !emitted.empty(), "nothing emitted");
  if(emitted.empty()) return;

  ReportTest("envelope key is \"action\" (not v0.8 \"userAction\")",
             emitted.find("\"action\":{") != std::string::npos &&
               emitted.find("userAction") == std::string::npos,
             "got: " + emitted);

  ReportTest("envelope carries \"version\"",
             emitted.find("\"version\":\"v0.9\"") != std::string::npos,
             "got: " + emitted);

  ReportTest("action carries required \"timestamp\"",
             emitted.find("\"timestamp\":\"") != std::string::npos,
             "got: " + emitted);

  ReportTest("action carries name/surfaceId/sourceComponentId",
             emitted.find("\"name\":\"submit_form\"") != std::string::npos &&
               emitted.find("\"surfaceId\":\"surface-1\"") != std::string::npos &&
               emitted.find("\"sourceComponentId\":\"submit_btn\"") != std::string::npos,
             "got: " + emitted);

  ReportTest("resolved context preserved",
             emitted.find("\"context\":{\"note\":\"hello\"}") != std::string::npos,
             "got: " + emitted);

  // `context` is required by the schema, so it must be emitted (as {}) even when
  // the component's action declares none.
  std::string noContext;
  DispatchFixtureAction(R"({"action":{"event":{"name":"ping"}}})", noContext);
  ReportTest("context emitted even when component declares none",
             noContext.find("\"context\":{}") != std::string::npos,
             "got: " + noContext);
}

// ========================================================================
// Test 10: `callFunction` envelope field positions
//
// Spec shape: functionCallId and wantResponse are siblings of `callFunction`
// at the envelope level; only call/args live inside the body.
// ========================================================================

void TestCallFunctionEnvelope()
{
  std::cout << "\n=== Test: callFunction Envelope Field Positions ===" << std::endl;

  A2uiMessageProcessor processor;
  ExpressionParser     exprParser;
  processor.SetExpressionParser(&exprParser);

  std::string capturedId;
  std::string capturedValue;
  int         responseCount = 0;
  processor.SetFunctionResponseCallback(
    [&](const std::string& id, const std::string& value) {
      capturedId    = id;
      capturedValue = value;
      responseCount++;
    });

  SurfaceModel surface;
  processor.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"fn-test","catalogId":"basic"}})",
    surface);

  bool ok = processor.ProcessLine(
    R"({"version":"v0.9","functionCallId":"fc_001","wantResponse":true,)"
    R"("callFunction":{"call":"formatString","args":{"value":"hi"}}})",
    surface);

  ReportTest("callFunction (spec envelope) parsed", ok,
             ok ? "" : processor.GetLastError());

  ReportTest("wantResponse read from envelope → response sent once",
             responseCount == 1,
             "responseCount=" + std::to_string(responseCount));

  ReportTest("functionCallId read from envelope",
             capturedId == "fc_001",
             "got: '" + capturedId + "'");

  ReportTest("function actually evaluated",
             capturedValue == "hi",
             "got: '" + capturedValue + "'");

  // wantResponse defaults to false → no response message.
  int quietCount = 0;
  A2uiMessageProcessor quiet;
  quiet.SetExpressionParser(&exprParser);
  quiet.SetFunctionResponseCallback(
    [&](const std::string&, const std::string&) { quietCount++; });
  SurfaceModel quietSurface;
  quiet.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"fn-quiet","catalogId":"basic"}})",
    quietSurface);
  quiet.ProcessLine(
    R"({"version":"v0.9","functionCallId":"fc_002",)"
    R"("callFunction":{"call":"formatString","args":{"value":"x"}}})",
    quietSurface);
  ReportTest("no response when wantResponse absent", quietCount == 0,
             "quietCount=" + std::to_string(quietCount));
}

// ========================================================================
// Test 11: A2UI media type
//
// IANA convention (upstream commit c712e0fe) → "application/a2ui+json".
// The pre-IANA spelling must still be accepted on receive for interop with
// agents that have not migrated.
// ========================================================================

void TestMimeType()
{
  std::cout << "\n=== Test: A2UI Media Type ===" << std::endl;

  ReportTest("outgoing media type is application/a2ui+json",
             std::string(A2UI_MIME_TYPE) == "application/a2ui+json",
             std::string("got: ") + A2UI_MIME_TYPE);

  ReportTest("accepts canonical application/a2ui+json",
             IsA2uiMimeType("application/a2ui+json"));

  ReportTest("accepts legacy application/json+a2ui",
             IsA2uiMimeType("application/json+a2ui"));

  ReportTest("rejects unrelated media type",
             !IsA2uiMimeType("text/plain"));

  ReportTest("unlabelled part treated as A2UI",
             IsA2uiMimeType(nullptr));
}

// ========================================================================
// Test 12: JSON Pointer auto-typing
//
// Spec rule: when a write creates intermediate nodes, a numeric segment must
// create an Array and anything else an Object. Writing an object for a numeric
// segment turns "/items/0/name" into {"items":{"0":…}}, which every data-driven
// list bound to /items then fails to read.
// ========================================================================
void TestJsonPointerAutoTyping()
{
  std::cout << "\n=== Test: JSON Pointer Auto-typing ===" << std::endl;

  {
    DataModel dm;
    dm.SetValue("/items/0/name", "x");
    ReportTest("numeric segment creates an Array (from empty)",
               dm.Serialize() == R"({"items":[{"name":"x"}]})", "got: " + dm.Serialize());
  }
  {
    DataModel dm;
    dm.SetData("/", R"({"obj":{}})");
    dm.SetValue("/obj/list/0", "v");
    ReportTest("numeric segment creates an Array (under existing object)",
               dm.Serialize() == R"({"obj":{"list":["v"]}})", "got: " + dm.Serialize());
  }
  {
    DataModel dm;
    dm.SetValue("/a/b/c", "x");
    ReportTest("non-numeric segments still create Objects",
               dm.Serialize() == R"({"a":{"b":{"c":"x"}}})", "got: " + dm.Serialize());
  }
  {
    // Index > 0 leaves the earlier slots empty rather than shifting the value down.
    DataModel dm;
    dm.SetValue("/rows/2", "third");
    ReportTest("index past the end pads the array",
               dm.Serialize() == R"({"rows":[null,null,"third"]})", "got: " + dm.Serialize());
  }
}

// ========================================================================
// Test 13: updateDataModel deletion
//
// v0.9.1 schema: "If omitted, the key at 'path' is removed."
// v1.0 makes `value` required and spells deletion as an explicit `value: null`.
// Both must delete — neither may be rejected or stored as a literal null.
// ========================================================================
void TestUpdateDataModelDeletion()
{
  std::cout << "\n=== Test: updateDataModel Deletion ===" << std::endl;

  auto seed = [](A2uiMessageProcessor& mp, SurfaceModel& s, const char* id) {
    mp.ProcessLine(std::string(R"({"version":"v0.9","createSurface":{"surfaceId":")") + id +
                   R"("}})", s);
    mp.ProcessLine(std::string(R"({"version":"v0.9","updateDataModel":{"surfaceId":")") + id +
                   R"(","path":"/","value":{"a":1,"b":2}}})", s);
  };

  {
    SurfaceModel s;
    A2uiMessageProcessor mp;
    seed(mp, s, "del1");
    bool ok = mp.ProcessLine(
      R"({"version":"v0.9","updateDataModel":{"surfaceId":"del1","path":"/a"}})", s);
    ReportTest("v0.9.1: omitted 'value' removes the key",
               ok && s.GetDataModel().Serialize() == R"({"b":2})",
               "accepted=" + std::to_string(ok) + " got: " + s.GetDataModel().Serialize());
  }
  {
    SurfaceModel s;
    A2uiMessageProcessor mp;
    seed(mp, s, "del2");
    bool ok = mp.ProcessLine(
      R"({"version":"v0.9","updateDataModel":{"surfaceId":"del2","path":"/a","value":null}})", s);
    ReportTest("v1.0: explicit 'value: null' removes the key",
               ok && s.GetDataModel().Serialize() == R"({"b":2})",
               "accepted=" + std::to_string(ok) + " got: " + s.GetDataModel().Serialize());
  }
  {
    // An array slot is emptied, not spliced out — later indices must not shift under the
    // components bound to them.
    SurfaceModel s;
    A2uiMessageProcessor mp;
    mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"del3"}})", s);
    mp.ProcessLine(R"({"version":"v0.9","updateDataModel":{"surfaceId":"del3","path":"/",)"
                   R"("value":{"arr":[1,2,3]}}})", s);
    mp.ProcessLine(R"({"version":"v0.9","updateDataModel":{"surfaceId":"del3","path":"/arr/1"}})", s);
    ReportTest("deleting an array slot preserves length",
               s.GetDataModel().Serialize() == R"({"arr":[1,null,3]})",
               "got: " + s.GetDataModel().Serialize());
  }
}

// ========================================================================
// Test 14: surface id uniqueness
//
// Blueprint: "It is an error to receive a createSurface message for a surfaceId
// that is already active." The id becomes free again after deleteSurface.
// ========================================================================
void TestSurfaceIdUniqueness()
{
  std::cout << "\n=== Test: Surface ID Uniqueness ===" << std::endl;

  {
    SurfaceModel s;
    A2uiMessageProcessor mp;
    bool first  = mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"u1"}})", s);
    bool second = mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"u1"}})", s);
    ReportTest("first createSurface is accepted", first, mp.GetLastError());
    ReportTest("duplicate createSurface is rejected", !second,
               "second accepted, lastError='" + mp.GetLastError() + "'");
  }
  {
    SurfaceModel s;
    A2uiMessageProcessor mp;
    mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"u2"}})", s);
    mp.ProcessLine(R"({"version":"v0.9","deleteSurface":{"surfaceId":"u2"}})", s);
    bool again = mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"u2"}})", s);
    ReportTest("id is reusable after deleteSurface", again, mp.GetLastError());
  }
  {
    // Two different ids on one processor must not collide.
    SurfaceModel a, b;
    A2uiMessageProcessor mp;
    bool first  = mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"u3"}})", a);
    bool second = mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"u4"}})", b);
    ReportTest("distinct surfaceIds both accepted", first && second, mp.GetLastError());
  }
}

// ========================================================================
// Test 15: type coercion + formatString escaping
// ========================================================================
void TestCoercionAndEscaping()
{
  std::cout << "\n=== Test: Type Coercion & Escaping ===" << std::endl;

  DataModel dm;
  dm.SetData("/", R"({"t":"TRUE","f":"False","other":"banana","n":5,"z":0})");

  ReportTest("string 'TRUE' coerces to true (case-insensitive)", dm.GetBool("/t"));
  ReportTest("string 'False' coerces to false", !dm.GetBool("/f"));
  // Any other string is false even when the caller's fallback is true.
  ReportTest("arbitrary string coerces to false, not the fallback",
             !dm.GetBool("/other", true));
  ReportTest("non-zero number coerces to true", dm.GetBool("/n"));
  ReportTest("zero coerces to false", !dm.GetBool("/z", true));

  DataContext ctx(dm, "/");
  ExpressionParser ep;
  auto eval = [&](const char* json) -> std::string {
    Dali::Ui::Integration::JsonParser p = Dali::Ui::Integration::JsonParser::New();
    if(!p.Parse(json)) return "<parse-error>";
    return ep.Evaluate(*p.GetRoot(), ctx);
  };

  ReportTest("escaped \\${ renders as a literal ${",
             eval(R"({"call":"formatString","args":{"value":"cost \\${5}"}})") == "cost ${5}",
             "got: " + eval(R"({"call":"formatString","args":{"value":"cost \\${5}"}})"));
  ReportTest("an unescaped ${…} still interpolates",
             eval(R"({"call":"formatString","args":{"value":"n=${/n}"}})") == "n=5",
             "got: " + eval(R"({"call":"formatString","args":{"value":"n=${/n}"}})"));
}

// ========================================================================
// Test 16: client data model sync (a2uiClientDataModel)
//
// A surface created with sendDataModel:true must report its data model back to
// the agent; one without the flag must contribute nothing.
// ========================================================================
void TestClientDataModelSync()
{
  std::cout << "\n=== Test: Client Data Model Sync ===" << std::endl;

  {
    SurfaceGroupModel group;
    A2uiMessageProcessor mp;
    SurfaceModel& s = group.GetOrCreateSurface("sync1");
    mp.ProcessLine(R"({"version":"v0.9","createSurface":{"surfaceId":"sync1"}})", s);
    mp.ProcessLine(R"({"version":"v0.9","updateDataModel":{"surfaceId":"sync1","path":"/",)"
                   R"("value":{"email":"a@b.c"}}})", s);
    ReportTest("no payload when sendDataModel is off",
               group.GetClientDataModel().empty(), "got: " + group.GetClientDataModel());
  }
  {
    SurfaceGroupModel group;
    A2uiMessageProcessor mp;
    SurfaceModel& s = group.GetOrCreateSurface("sync2");
    mp.ProcessLine(
      R"({"version":"v0.9","createSurface":{"surfaceId":"sync2","sendDataModel":true}})", s);
    mp.ProcessLine(R"({"version":"v0.9","updateDataModel":{"surfaceId":"sync2","path":"/",)"
                   R"("value":{"email":"a@b.c"}}})", s);
    const std::string expected =
      std::string(R"({"version":")") + A2UI_PROTOCOL_VERSION +
      R"(","surfaces":{"sync2":{"email":"a@b.c"}}})";
    ReportTest("sendDataModel surface reports its data model",
               group.GetClientDataModel() == expected, "got: " + group.GetClientDataModel());
  }
}

// ========================================================================
// Test 12: v1.0 forward-compatible message shapes
//
// v1.0 renames createSurface.theme to surfaceProperties, lets createSurface carry the
// initial components/dataModel inline, and spells a deletion as an explicit
// `value: null` (v0.9.1 spells the same thing as an omitted `value`). All are accepted
// alongside the v0.9 spellings, which keep working.
// ========================================================================

void TestSurfaceProperties()
{
  std::cout << "\n=== Test: createSurface surfaceProperties (v1.0 name) ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  bool ok = processor.ProcessLine(
    R"({"version":"v1.0","createSurface":{"surfaceId":"sp","catalogId":"basic",)"
    R"("surfaceProperties":{"width":480,"height":1280,"pattern":"card","agentDisplayName":"A"}}})",
    surface);

  ReportTest("createSurface with surfaceProperties parsed", ok,
             ok ? "" : processor.GetLastError());
  ReportTest("surfaceProperties width applied", surface.GetPreferWidth() == 480.0f,
             "got " + std::to_string(surface.GetPreferWidth()));
  ReportTest("surfaceProperties pattern applied", surface.GetPattern() == "card",
             "got '" + surface.GetPattern() + "'");

  // The v0.9 `theme` spelling must keep working.
  A2uiMessageProcessor legacy;
  SurfaceModel legacySurface;
  legacy.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"th","catalogId":"basic",)"
    R"("theme":{"width":320,"height":640,"pattern":"plain"}}})",
    legacySurface);
  ReportTest("legacy theme still applied", legacySurface.GetPreferWidth() == 320.0f,
             "got " + std::to_string(legacySurface.GetPreferWidth()));
}

void TestInlineCreateSurface()
{
  std::cout << "\n=== Test: createSurface with inline components/dataModel ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  bool ok = processor.ProcessLine(
    R"({"version":"v1.0","createSurface":{"surfaceId":"inline","catalogId":"basic",)"
    R"("dataModel":{"title":"Inline UI"},)"
    R"("components":[{"id":"root","component":"Text","text":{"path":"/title"}}]}})",
    surface);

  ReportTest("inline createSurface parsed", ok, ok ? "" : processor.GetLastError());
  ReportTest("inline components populated the tree", surface.GetComponentCount() == 1,
             "count=" + std::to_string(surface.GetComponentCount()));

  const auto* root = surface.GetComponentsModel().GetRoot();
  ReportTest("inline root is the Text component", root && root->type == "Text",
             root ? "got: " + root->type : "no root");
  ReportTest("inline dataModel populated",
             surface.GetDataModel().GetString("/title") == "Inline UI",
             "got '" + surface.GetDataModel().GetString("/title") + "'");
}

void TestNullValueDeletes()
{
  std::cout << "\n=== Test: updateDataModel value:null deletes the key ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;
  processor.ProcessLine(
    R"({"version":"v0.9","createSurface":{"surfaceId":"del","catalogId":"basic"}})", surface);
  processor.ProcessLine(
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"del","path":"/","value":{"keep":"a","drop":"b"}}})",
    surface);

  ReportTest("precondition: both keys present",
             surface.GetDataModel().GetString("/drop") == "b");

  bool ok = processor.ProcessLine(
    R"({"version":"v1.0","updateDataModel":{"surfaceId":"del","path":"/drop","value":null}})",
    surface);

  ReportTest("value:null accepted", ok, ok ? "" : processor.GetLastError());
  // Assert on the serialized model, not just ResolvePath: a stored null that merely fails
  // to resolve would look identical through the pointer API but still ship in the payload.
  ReportTest("key at path is gone from the model",
             surface.GetDataModel().Serialize() == R"({"keep":"a"})",
             "got " + surface.GetDataModel().Serialize());

  // v0.9.1 spells the same deletion as an omitted `value`; v1.0 requires the explicit
  // null. The renderer accepts both so either version's agents can clear a key.
  bool omitted = processor.ProcessLine(
    R"({"version":"v0.9.1","updateDataModel":{"surfaceId":"del","path":"/keep"}})", surface);
  ReportTest("omitted value deletes too (v0.9.1 spelling)",
             omitted && surface.GetDataModel().Serialize() == "{}",
             "ok=" + std::to_string(omitted) + " model=" + surface.GetDataModel().Serialize());
}

// ========================================================================
// Test 13: the built-in @index function
//
// Returns the 0-based iteration index inside a list template, plus an optional
// offset. Outside a collection scope it must not evaluate.
// ========================================================================

void TestIndexFunction()
{
  std::cout << "\n=== Test: @index built-in ===" << std::endl;

  DataModel model;
  model.SetData("/", R"({"items":["a","b","c"]})");
  ExpressionParser parser;

  Dali::Ui::Integration::JsonParser jp = Dali::Ui::Integration::JsonParser::New();
  jp.Parse(R"({"plain":{"call":"@index","args":{}},)"
           R"("offset":{"call":"@index","args":{"offset":1}}})");
  const TreeNode* plain  = jp.GetRoot()->Find("plain");
  const TreeNode* offset = jp.GetRoot()->Find("offset");

  DataContext root(model);
  DataContext item1 = root.CreateCollectionItemContext("/items/1", 1);

  ReportTest("@index inside a collection scope returns the index",
             parser.Evaluate(*plain, item1) == "1",
             "got '" + parser.Evaluate(*plain, item1) + "'");

  ReportTest("@index applies the offset argument",
             parser.Evaluate(*offset, item1) == "2",
             "got '" + parser.Evaluate(*offset, item1) + "'");

  DataContext item0 = root.CreateChildContextForIndex(0);
  ReportTest("@index is 0-based",
             parser.Evaluate(*plain, item0) == "0",
             "got '" + parser.Evaluate(*plain, item0) + "'");

  ReportTest("@index outside a collection scope does not evaluate",
             parser.Evaluate(*plain, root).empty(),
             "got '" + parser.Evaluate(*plain, root) + "'");
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
  TestActionEnvelopeConformance();
  TestCallFunctionEnvelope();
  TestMimeType();
  TestJsonPointerAutoTyping();
  TestUpdateDataModelDeletion();
  TestSurfaceIdUniqueness();
  TestCoercionAndEscaping();
  TestClientDataModelSync();
  TestSurfaceProperties();
  TestInlineCreateSurface();
  TestNullValueDeletes();
  TestIndexFunction();

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
