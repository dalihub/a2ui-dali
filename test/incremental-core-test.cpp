/* Copyright (c) 2026 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Core-layer test for incremental rendering support structures.
 *
 * Tests the SurfaceComponentsModel parent map and GetChildIndexInParent
 * without requiring a DALi display. These are the foundation that
 * RenderIncremental relies on to splice new views into the right parent.
 */

#include "core/surface-components-model.h"
#include "core/surface-model.h"
#include "core/a2ui-message-processor.h"

#include <dali-ui-foundation/integration-api/builder/json-parser.h>
#include <iostream>
#include <string>
#include <vector>

using Dali::Ui::Integration::JsonParser;
using Dali::Ui::Integration::TreeNode;
using A2ui::SurfaceComponentsModel;
using A2ui::SurfaceModel;
using A2ui::A2uiMessageProcessor;

namespace
{
int gTotal  = 0;
int gPassed = 0;
int gFailed = 0;


struct Result { std::string name; bool passed; std::string error; };
std::vector<Result> gResults;

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

/// Parse a JSON string into a TreeNode. The JsonParser must outlive the
/// returned TreeNode — callers keep the parser alive in a local variable.
const TreeNode* ParseJson(JsonParser& parser, const std::string& json)
{
  parser.Parse(json);
  return parser.GetRoot();
}

/// Build a components model from a JSON array string of components.
/// Keeps the JsonParser alive for the lifetime of the call so the
/// TreeNode pointers AddComponents reads remain valid.
SurfaceComponentsModel BuildModel(const std::string& componentsJson)
{
  JsonParser parser = JsonParser::New();
  SurfaceComponentsModel model;
  const TreeNode* root = ParseJson(parser, componentsJson);
  if(root)
  {
    model.AddComponents(*root);
  }
  return model;
}


// ========================================================================
// Test 1: Parent map — basic parent-child lookup
// ========================================================================
void TestParentMapBasic()
{
  std::cout << "\n=== Test: Parent Map — Basic Lookup ===" << std::endl;

  // root → row → [col1, col2]
  //              col1 → [text1]
  //              col2 → [text2]
  std::string json = R"([
    {"id":"root","component":"Card","child":"row"},
    {"id":"row","component":"Row","children":["col1","col2"]},
    {"id":"col1","component":"Column","children":["text1"]},
    {"id":"col2","component":"Column","children":["text2"]},
    {"id":"text1","component":"Text","text":"hello"},
    {"id":"text2","component":"Text","text":"world"}
  ])";

  SurfaceComponentsModel model = BuildModel(json);

  // root's parent should be empty (root has no parent)
  ReportTest("root has no parent", model.GetParentId("root") == "",
             "expected empty, got '" + model.GetParentId("root") + "'");

  // row's parent is root (child property)
  ReportTest("row's parent is root (single child)", model.GetParentId("row") == "root",
             "expected 'root', got '" + model.GetParentId("row") + "'");

  // col1's parent is row (children array)
  ReportTest("col1's parent is row (array child)", model.GetParentId("col1") == "row",
             "expected 'row', got '" + model.GetParentId("col1") + "'");

  // col2's parent is row
  ReportTest("col2's parent is row", model.GetParentId("col2") == "row",
             "expected 'row', got '" + model.GetParentId("col2") + "'");

  // text1's parent is col1
  ReportTest("text1's parent is col1", model.GetParentId("text1") == "col1",
             "expected 'col1', got '" + model.GetParentId("text1") + "'");

  // Nonexistent component
  ReportTest("nonexistent component has empty parent", model.GetParentId("nonexistent") == "",
             "expected empty for nonexistent");
}

// ========================================================================
// Test 2: GetChildIndexInParent — index within children array
// ========================================================================
void TestChildIndexInParent()
{
  std::cout << "\n=== Test: Child Index in Parent ===" << std::endl;

  std::string json = R"([
    {"id":"root","component":"Column","children":["a","b","c","d"]},
    {"id":"a","component":"Text","text":"A"},
    {"id":"b","component":"Text","text":"B"},
    {"id":"c","component":"Text","text":"C"},
    {"id":"d","component":"Text","text":"D"}
  ])";

  SurfaceComponentsModel model = BuildModel(json);

  ReportTest("child 'a' is at index 0", model.GetChildIndexInParent("a") == 0,
             "expected 0, got " + std::to_string(model.GetChildIndexInParent("a")));
  ReportTest("child 'b' is at index 1", model.GetChildIndexInParent("b") == 1,
             "expected 1, got " + std::to_string(model.GetChildIndexInParent("b")));
  ReportTest("child 'c' is at index 2", model.GetChildIndexInParent("c") == 2,
             "expected 2, got " + std::to_string(model.GetChildIndexInParent("c")));
  ReportTest("child 'd' is at index 3", model.GetChildIndexInParent("d") == 3,
             "expected 3, got " + std::to_string(model.GetChildIndexInParent("d")));

  // Single child (Card's child property) → index 0
  std::string json2 = R"([
    {"id":"root","component":"Card","child":"content"},
    {"id":"content","component":"Text","text":"hi"}
  ])";
  SurfaceComponentsModel model2 = BuildModel(json2);
  ReportTest("single child (Card) is at index 0", model2.GetChildIndexInParent("content") == 0,
             "expected 0, got " + std::to_string(model2.GetChildIndexInParent("content")));

  // Root has no parent → -1
  ReportTest("root child index is -1 (no parent)", model.GetChildIndexInParent("root") == -1,
             "expected -1, got " + std::to_string(model.GetChildIndexInParent("root")));

  // Nonexistent → -1
  ReportTest("nonexistent child index is -1", model.GetChildIndexInParent("nonexistent") == -1,
             "expected -1, got " + std::to_string(model.GetChildIndexInParent("nonexistent")));
}

// ========================================================================
// Test 3: Parent map survives upsert (re-Add same component)
// ========================================================================
void TestParentMapUpsert()
{
  std::cout << "\n=== Test: Parent Map — Upsert (Re-add) ===" << std::endl;

  // First add: root with child "a"
  SurfaceComponentsModel model = BuildModel(R"([
    {"id":"root","component":"Column","children":["a"]},
    {"id":"a","component":"Text","text":"A"}
  ])");

  ReportTest("before upsert: a's parent is root", model.GetParentId("a") == "root");

  // Re-add root with different children (now "a" and "b")
  JsonParser parser2 = JsonParser::New();
  const TreeNode* root2 = ParseJson(parser2, R"([
    {"id":"root","component":"Column","children":["a","b"]},
    {"id":"b","component":"Text","text":"B"}
  ])");
  if(root2) model.AddComponents(*root2);


  ReportTest("after upsert: a's parent is still root", model.GetParentId("a") == "root",
             "expected 'root', got '" + model.GetParentId("a") + "'");
  ReportTest("after upsert: b's parent is root", model.GetParentId("b") == "root",
             "expected 'root', got '" + model.GetParentId("b") + "'");
  ReportTest("after upsert: a is at index 0", model.GetChildIndexInParent("a") == 0);
  ReportTest("after upsert: b is at index 1", model.GetChildIndexInParent("b") == 1);
}

// ========================================================================
// Test 4: Parent map cleared on Clear()
// ========================================================================
void TestParentMapClear()
{
  std::cout << "\n=== Test: Parent Map — Clear ===" << std::endl;

  SurfaceComponentsModel model = BuildModel(R"([
    {"id":"root","component":"Column","children":["a"]},
    {"id":"a","component":"Text","text":"A"}
  ])");

  ReportTest("before clear: a's parent is root", model.GetParentId("a") == "root");

  model.Clear();

  ReportTest("after clear: a's parent is empty", model.GetParentId("a") == "",
             "expected empty after clear");
  ReportTest("after clear: component count is 0", model.GetCount() == 0);
}

// ========================================================================
// Test 5: GetLastModifiedIds tracks upserted components
// ========================================================================
void TestLastModifiedIds()
{
  std::cout << "\n=== Test: LastModifiedIds ===" << std::endl;

  SurfaceComponentsModel model = BuildModel(R"([
    {"id":"root","component":"Column","children":["a","b"]},
    {"id":"a","component":"Text","text":"A"},
    {"id":"b","component":"Text","text":"B"}
  ])");

  const auto& mods = model.GetLastModifiedIds();
  ReportTest("3 components modified", mods.size() == 3,
             "expected 3, got " + std::to_string(mods.size()));

  // Second add: only "b" is re-added
  JsonParser parser3 = JsonParser::New();
  const TreeNode* root2 = ParseJson(parser3, R"([
    {"id":"b","component":"Text","text":"B-updated"}
  ])");
  if(root2) model.AddComponents(*root2);


  const auto& mods2 = model.GetLastModifiedIds();
  ReportTest("1 component modified in second add", mods2.size() == 1,
             "expected 1, got " + std::to_string(mods2.size()));
  ReportTest("modified component is 'b'", mods2[0] == "b",
             "expected 'b', got '" + mods2[0] + "'");
}

// ========================================================================
// Test 6: Deep nesting — 5 levels
// ========================================================================
void TestDeepNesting()
{
  std::cout << "\n=== Test: Deep Nesting (5 levels) ===" << std::endl;

  std::string json = R"([
    {"id":"root","component":"Column","children":["l1"]},
    {"id":"l1","component":"Column","children":["l2"]},
    {"id":"l2","component":"Column","children":["l3"]},
    {"id":"l3","component":"Column","children":["l4"]},
    {"id":"l4","component":"Text","text":"deep"}
  ])";

  SurfaceComponentsModel model = BuildModel(json);

  ReportTest("l4's parent is l3", model.GetParentId("l4") == "l3");
  ReportTest("l3's parent is l2", model.GetParentId("l3") == "l2");
  ReportTest("l2's parent is l1", model.GetParentId("l2") == "l1");
  ReportTest("l1's parent is root", model.GetParentId("l1") == "root");
  ReportTest("root's parent is empty", model.GetParentId("root") == "");
}

// ========================================================================
// Test 7: Full message processor flow (end-to-end with processor)
// ========================================================================
void TestMessageProcessorFlow()
{
  std::cout << "\n=== Test: Message Processor Flow ===" << std::endl;

  A2uiMessageProcessor processor;
  SurfaceModel surface;

  // createSurface
  std::string createMsg = R"({"version":"v0.9","createSurface":{"surfaceId":"test","catalogId":"basic"}})";
  ReportTest("createSurface", processor.ProcessLine(createMsg, surface));

  // updateComponents with a simple tree
  std::string compMsg = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[
    {"id":"root","component":"Column","children":["title","body"]},
    {"id":"title","component":"Text","text":"Hello"},
    {"id":"body","component":"Text","text":"World"}
  ]}})";
  ReportTest("updateComponents", processor.ProcessLine(compMsg, surface));

  const SurfaceComponentsModel& comps = surface.GetComponentsModel();
  ReportTest("root exists", comps.GetRoot() != nullptr);
  ReportTest("title's parent is root", comps.GetParentId("title") == "root");
  ReportTest("body's parent is root", comps.GetParentId("body") == "root");
  ReportTest("title is at index 0", comps.GetChildIndexInParent("title") == 0);
  ReportTest("body is at index 1", comps.GetChildIndexInParent("body") == 1);

  // Second updateComponents: only "body" changes (text updated)
  std::string compMsg2 = R"({"version":"v0.9","updateComponents":{"surfaceId":"test","components":[
    {"id":"body","component":"Text","text":"Updated World"}
  ]}})";
  ReportTest("second updateComponents (body only)", processor.ProcessLine(compMsg2, surface));

  const auto& mods = comps.GetLastModifiedIds();
  ReportTest("only 'body' was modified", mods.size() == 1 && mods[0] == "body",
             "expected [body], got [" + (mods.empty() ? "" : mods[0]) + "]");

  // Parent map should still be intact
  ReportTest("body's parent still root after update", comps.GetParentId("body") == "root");
}


} // anonymous namespace

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  std::cout << "========================================" << std::endl;
  std::cout << "  a2ui-dali incremental core test" << std::endl;
  std::cout << "========================================" << std::endl;

  TestParentMapBasic();
  TestChildIndexInParent();
  TestParentMapUpsert();
  TestParentMapClear();
  TestLastModifiedIds();
  TestDeepNesting();
  TestMessageProcessorFlow();

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
