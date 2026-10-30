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
 * Regression test for the src/stream streaming parser (StreamingContentParser +
 * ProtocolStreamExtractor + Text/Markdown plugins). No DALi needed.
 *
 * Every scenario is a list of A2UI messages. The messages are joined into one byte
 * stream (concatenated, newline-separated JSONL, or CRLF-separated) and fed to the
 * parser unsplit, split at every 2-way position, and byte by byte. For each run the
 * emitted events are applied to a small reference model (surface lifecycle,
 * components by id, data model per surface) and compared with the model obtained by
 * applying the original messages directly. The data model is checked twice: in a small
 * nlohmann-based reference model and in the renderer's real A2uiMessageProcessor /
 * DataModel (no display needed). This checks, for any chunking:
 *   - every emitted event is valid JSON with valid UTF-8 (no split escape /
 *     surrogate / multi-byte sequence leaks out of streamed text);
 *   - the order of message kinds matches the source;
 *   - createSurface / deleteSurface messages arrive exactly once, unchanged;
 *   - components converge to the source components;
 *   - the data model converges to the one the unsplit messages produce, also under the
 *     renderer's own rules (e.g. "value":null deletes);
 *   - "version" is preserved (or stays absent) on every event;
 *   - malformed messages are passed through exactly as unsplit, and parsing is linear.
 *
 *   ./a2ui-stream-parser-test [repo-root] [-v]
 *
 * repo-root defaults to ".." (run from build/ or bin/). -v keeps the parser's
 * stderr log; it is silenced by default because the byte-by-byte runs are noisy.
 */

#include "stream/streaming-content-parser.h"
#include "stream/protocol-stream-extractor.h"
#include "stream/stream-event-sink.h"
#include "stream/error-code.h"
#include "core/a2ui-message-processor.h"
#include "core/surface-model.h"

#include "nlohmann/json.hpp"

#include <cstdio>
#include <dirent.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

static int gTotal = 0;
static int gPassed = 0;
static int gFailed = 0;
static std::vector<std::string> gFailures;

static void ReportTest(const std::string& name, bool passed, const std::string& error = "")
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
    gFailures.push_back(name + " — " + error);
  }
}

// ========================================================================
// Parser driver
// ========================================================================

struct Event
{
  std::string type;
  std::string data;
};

class CollectingSink : public A2ui::IStreamEventSink
{
public:
  std::vector<Event> events;

  A2ui::AGenUIExeCode createSurface(const std::string& d) override { return Add("createSurface", d); }
  A2ui::AGenUIExeCode deleteSurface(const std::string& d) override { return Add("deleteSurface", d); }
  A2ui::AGenUIExeCode updateComponents(const std::string& d) override { return Add("updateComponents", d); }
  A2ui::AGenUIExeCode updateDataModel(const std::string& d) override { return Add("updateDataModel", d); }
  A2ui::AGenUIExeCode appendDataModel(const std::string& d) override { return Add("appendDataModel", d); }

private:
  A2ui::AGenUIExeCode Add(const char* type, const std::string& d)
  {
    events.push_back({type, d});
    return A2ui::Execute_Success;
  }
};

static std::vector<Event> RunChunks(const std::vector<std::string>& chunks)
{
  CollectingSink sink;
  A2ui::StreamingContentParser parser(&sink);
  parser.processDataBeginning();
  for(const auto& c : chunks)
  {
    parser.processDataAssembling(c);
  }
  parser.processDataEnding();
  return sink.events;
}

static std::vector<std::string> Chunk(const std::string& s, size_t n)
{
  std::vector<std::string> chunks;
  for(size_t i = 0; i < s.size(); i += n) chunks.push_back(s.substr(i, n));
  return chunks;
}

// ========================================================================
// Reference model
// ========================================================================

static const char* kMessageKeys[] = {
  "createSurface", "updateComponents", "updateDataModel", "appendDataModel", "deleteSurface"};

struct Model
{
  std::vector<std::string> lifecycle;                               // createSurface/deleteSurface dumps, in order
  std::map<std::string, std::map<std::string, json>> components;   // surfaceId -> id -> component
  std::map<std::string, json> data;                                 // surfaceId -> data model
  std::set<std::string> versions;                                   // "kind|version"
  std::vector<std::string> kinds;                                   // message kinds, consecutive runs collapsed
  std::map<std::string, std::string> rendered;                      // surfaceId -> real renderer DataModel
  std::string error;                                                // first error, empty when consistent

  void Fail(const std::string& e)
  {
    if(error.empty()) error = e;
  }
};

static bool SplitPointer(const std::string& path, std::vector<std::string>& tokens)
{
  tokens.clear();
  if(path.empty() || path == "/") return true; // the renderer treats both as the root
  if(path[0] != '/') return false;
  size_t start = 1;
  while(true)
  {
    size_t end = path.find('/', start);
    std::string tok = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string decoded;
    for(size_t i = 0; i < tok.size(); ++i)
    {
      if(tok[i] == '~' && i + 1 < tok.size() && (tok[i + 1] == '0' || tok[i + 1] == '1'))
      {
        decoded += (tok[i + 1] == '1') ? '/' : '~';
        ++i;
      }
      else
      {
        decoded += tok[i];
      }
    }
    tokens.push_back(decoded);
    if(end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

static bool ParseIndex(const std::string& tok, size_t& idx)
{
  if(tok.empty() || tok.size() > 9 || tok.find_first_not_of("0123456789") != std::string::npos) return false;
  idx = static_cast<size_t>(std::stoul(tok));
  return true;
}

// JSON-pointer write. Strict (default): parents must already exist (no implicit
// container creation), an array index may be at most size() (== size() appends).
// gLenient mirrors the renderer's DataModel for the repo samples instead: missing
// parents are created (numeric child -> array) and impossible writes are ignored.
// mode: 0 = set, 1 = append string, 2 = delete.
static bool gLenient = false;

static bool PointerApply(json& root, const std::string& path, const json& value, int mode, std::string& err)
{
  std::vector<std::string> tokens;
  if(!SplitPointer(path, tokens))
  {
    err = "bad pointer '" + path + "'";
    return false;
  }
  if(tokens.empty())
  {
    if(mode == 0) root = value;
    else if(mode == 2) root = json::object();
    else
    {
      if(!root.is_string() || !value.is_string()) { err = "append to non-string root"; return false; }
      root = root.get<std::string>() + value.get<std::string>();
    }
    return true;
  }
  json* cur = &root;
  for(size_t i = 0; i + 1 < tokens.size(); ++i)
  {
    const std::string& tok = tokens[i];
    if(cur->is_object())
    {
      if(!cur->contains(tok))
      {
        if(!gLenient) { err = "missing parent '" + tok + "' in " + path; return false; }
        size_t unused = 0;
        (*cur)[tok] = ParseIndex(tokens[i + 1], unused) ? json::array() : json::object();
      }
      cur = &(*cur)[tok];
    }
    else if(cur->is_array())
    {
      size_t idx = 0;
      if(!ParseIndex(tok, idx) || idx >= cur->size()) { err = "bad index '" + tok + "' in " + path; return false; }
      cur = &(*cur)[idx];
    }
    else
    {
      err = "non-container parent in " + path;
      return false;
    }
  }
  const std::string& last = tokens.back();
  if(cur->is_object())
  {
    if(mode == 2) { cur->erase(last); return true; }
    if(mode == 1)
    {
      if(!cur->contains(last) || !(*cur)[last].is_string() || !value.is_string())
      {
        err = "append to missing/non-string " + path;
        return false;
      }
      (*cur)[last] = (*cur)[last].get<std::string>() + value.get<std::string>();
      return true;
    }
    (*cur)[last] = value;
    return true;
  }
  if(cur->is_array())
  {
    size_t idx = cur->size();
    if(last != "-" && !ParseIndex(last, idx)) { err = "bad index in " + path; return false; }
    if(idx > cur->size()) { err = "sparse index in " + path; return false; }
    if(mode == 2) { if(idx < cur->size()) (*cur)[idx] = nullptr; return true; } // slot emptied, length kept
    if(mode == 1)
    {
      if(idx >= cur->size() || !(*cur)[idx].is_string() || !value.is_string())
      {
        err = "append to missing/non-string " + path;
        return false;
      }
      (*cur)[idx] = (*cur)[idx].get<std::string>() + value.get<std::string>();
      return true;
    }
    if(idx == cur->size()) cur->push_back(value);
    else (*cur)[idx] = value;
    return true;
  }
  err = "non-container parent of " + path;
  return false;
}

static std::string KindOf(const json& msg)
{
  if(!msg.is_object()) return "";
  for(auto it = msg.begin(); it != msg.end(); ++it)
  {
    for(const char* k : kMessageKeys)
    {
      if(it.key() == k) return k;
    }
  }
  return "";
}

// Applies one message (a source message, or an event emitted by the parser).
static void ApplyMessage(Model& m, const std::string& kind, const json& msg)
{
  if(!msg.is_object() || !msg.contains(kind) || !msg[kind].is_object())
  {
    m.Fail("event without '" + kind + "' object: " + msg.dump());
    return;
  }
  for(auto it = msg.begin(); it != msg.end(); ++it)
  {
    if(it.key() != kind && it.key() != "version")
    {
      m.Fail("unexpected top-level key '" + it.key() + "' in " + kind + ": " + msg.dump().substr(0, 200));
    }
  }
  std::string collapsed = (kind == "appendDataModel") ? "updateDataModel" : kind;
  if(m.kinds.empty() || m.kinds.back() != collapsed) m.kinds.push_back(collapsed);
  std::string version = "<none>";
  if(msg.contains("version"))
  {
    version = msg["version"].is_string() ? msg["version"].get<std::string>() : msg["version"].dump();
  }
  m.versions.insert(collapsed + "|" + version);

  const json& body = msg[kind];
  std::string sid = (body.contains("surfaceId") && body["surfaceId"].is_string()) ? body["surfaceId"].get<std::string>() : "";
  if(sid.empty())
  {
    m.Fail(kind + " without surfaceId: " + msg.dump().substr(0, 200));
    return;
  }

  if(kind == "createSurface" || kind == "deleteSurface")
  {
    m.lifecycle.push_back(msg.dump());
    return;
  }

  if(kind == "updateComponents")
  {
    if(!body.contains("components") || !body["components"].is_array())
    {
      m.Fail("updateComponents without components array: " + msg.dump().substr(0, 200));
      return;
    }
    auto& comps = m.components[sid];
    for(const auto& c : body["components"])
    {
      if(!c.is_object() || !c.contains("id") || !c["id"].is_string())
      {
        m.Fail("component without id: " + c.dump().substr(0, 200));
        continue;
      }
      std::string id = c["id"].get<std::string>();
      const char* chunkKey = c.contains("textChunk") ? "textChunk" : (c.contains("appendContent") ? "appendContent" : nullptr);
      if(chunkKey)
      {
        const char* field = (std::string(chunkKey) == "textChunk") ? "text" : "content";
        if(!comps.count(id) || !comps[id].contains(field) || !comps[id][field].is_string() || !c[chunkKey].is_string())
        {
          m.Fail(std::string(chunkKey) + " for component '" + id + "' without a streamed " + field);
          continue;
        }
        comps[id][field] = comps[id][field].get<std::string>() + c[chunkKey].get<std::string>();
        for(auto f = c.begin(); f != c.end(); ++f)
        {
          if(f.key() != "id" && f.key() != "component" && f.key() != chunkKey) comps[id][f.key()] = f.value();
        }
      }
      else
      {
        comps[id] = c;
      }
    }
    return;
  }

  // updateDataModel / appendDataModel
  if(!m.data.count(sid)) m.data[sid] = json::object();
  std::string path = "/";
  if(body.contains("path"))
  {
    if(!body["path"].is_string())
    {
      m.Fail("non-string path: " + msg.dump().substr(0, 200));
      return;
    }
    path = body["path"].get<std::string>();
  }
  // Like the renderer: an omitted value or "value":null deletes
  bool hasValue = body.contains("value") && !body["value"].is_null();
  int mode = (kind == "appendDataModel") ? 1 : (hasValue ? 0 : 2);
  std::string err;
  if(!PointerApply(m.data[sid], path, body.contains("value") ? body["value"] : json(), mode, err) && !gLenient)
  {
    m.Fail(kind + ": " + err + " :: " + msg.dump().substr(0, 200));
  }
}

// Applies messages to the renderer's real message processor, one SurfaceModel per surface.
// appendDataModel is not an A2UI message; it is emulated as "read string, write it back longer".
struct RendererRun
{
  A2ui::A2uiMessageProcessor processor;
  std::map<std::string, std::unique_ptr<A2ui::SurfaceModel>> surfaces;

  void Apply(const std::string& kind, const json& msg, const std::string& raw)
  {
    if(!msg.contains(kind) || !msg[kind].is_object() || !msg[kind].contains("surfaceId")) return;
    std::string sid = msg[kind]["surfaceId"].is_string() ? msg[kind]["surfaceId"].get<std::string>() : "";
    auto& surface = surfaces[sid];
    if(!surface) surface.reset(new A2ui::SurfaceModel());
    if(kind == "appendDataModel")
    {
      const json& body = msg[kind];
      std::string path = body.value("path", "/");
      std::string current = surface->GetDataModel().GetString(path);
      surface->GetDataModel().SetValue(path, current + body.value("value", ""));
      return;
    }
    processor.ProcessLine(raw, *surface);
  }

  std::map<std::string, std::string> Dump() const
  {
    std::map<std::string, std::string> out;
    for(const auto& s : surfaces)
    {
      // Compared verbatim: normalising through nlohmann would hide duplicate keys
      out[s.first] = s.second->GetDataModel().Serialize();
    }
    return out;
  }
};

static Model ModelFromSource(const std::vector<std::string>& messages)
{
  Model m;
  RendererRun renderer;
  for(const auto& s : messages)
  {
    json j = json::parse(s, nullptr, false);
    if(j.is_discarded())
    {
      m.Fail("source message is not valid JSON: " + s.substr(0, 120));
      continue;
    }
    ApplyMessage(m, KindOf(j), j);
    renderer.Apply(KindOf(j), j, s);
  }
  m.rendered = renderer.Dump();
  return m;
}

static Model ModelFromEvents(const std::vector<Event>& events)
{
  Model m;
  RendererRun renderer;
  for(const auto& e : events)
  {
    json j = json::parse(e.data, nullptr, false); // strict: rejects invalid UTF-8 and lone surrogates
    if(j.is_discarded())
    {
      m.Fail("invalid JSON in " + e.type + " event: " + e.data.substr(0, 200));
      continue;
    }
    ApplyMessage(m, e.type, j);
    renderer.Apply(e.type, j, e.data);
  }
  m.rendered = renderer.Dump();
  return m;
}

static std::string Join(const std::vector<std::string>& v)
{
  std::string s;
  for(const auto& x : v) s += (s.empty() ? "" : ",") + x;
  return s;
}

// Returns an empty string when `got` matches `want`, otherwise a description.
static std::string CompareModels(const Model& want, const Model& got)
{
  if(!got.error.empty()) return got.error;
  if(got.kinds != want.kinds) return "kind order [" + Join(got.kinds) + "] != [" + Join(want.kinds) + "]";
  if(got.lifecycle != want.lifecycle)
  {
    return "createSurface/deleteSurface events differ (" + std::to_string(got.lifecycle.size()) + " vs " +
           std::to_string(want.lifecycle.size()) + ")";
  }
  if(got.versions != want.versions)
  {
    std::vector<std::string> g(got.versions.begin(), got.versions.end());
    std::vector<std::string> w(want.versions.begin(), want.versions.end());
    return "versions {" + Join(g) + "} != {" + Join(w) + "}";
  }
  if(got.components != want.components)
  {
    for(const auto& s : want.components)
    {
      auto gs = got.components.find(s.first);
      if(gs == got.components.end()) return "components of surface '" + s.first + "' missing";
      for(const auto& c : s.second)
      {
        auto gc = gs->second.find(c.first);
        if(gc == gs->second.end()) return "component '" + c.first + "' missing";
        if(gc->second != c.second) return "component '" + c.first + "' = " + gc->second.dump() + " want " + c.second.dump();
      }
    }
    return "extra components emitted";
  }
  if(got.data != want.data)
  {
    for(const auto& s : want.data)
    {
      auto gd = got.data.find(s.first);
      std::string g = (gd == got.data.end()) ? "<none>" : gd->second.dump();
      if(g != s.second.dump()) return "data model of '" + s.first + "' = " + g.substr(0, 300) + " want " + s.second.dump().substr(0, 300);
    }
    return "extra data model emitted";
  }
  if(got.rendered != want.rendered)
  {
    for(const auto& s : want.rendered)
    {
      auto gr = got.rendered.find(s.first);
      std::string g = (gr == got.rendered.end()) ? "<none>" : gr->second;
      if(g != s.second) return "renderer data model of '" + s.first + "' = " + g.substr(0, 300) + " want " + s.second.substr(0, 300);
    }
    return "renderer: extra surfaces";
  }
  return "";
}

static std::string Visible(const std::string& s)
{
  std::string out;
  for(char c : s)
  {
    if(c == '\n') out += "\\n";
    else if(c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

// Feeds `stream` unsplit, at every 2-way split, and byte by byte; one check per mode.
static void CheckStream(const std::string& name, const std::string& stream, const Model& want, bool allSplits = true)
{
  // (a) unsplit
  {
    std::string err = CompareModels(want, ModelFromEvents(RunChunks({stream})));
    ReportTest(name + " unsplit", err.empty(), err);
  }
  // (b) every 2-way split
  if(allSplits)
  {
    std::string err;
    for(size_t i = 1; i < stream.size() && err.empty(); ++i)
    {
      err = CompareModels(want, ModelFromEvents(RunChunks({stream.substr(0, i), stream.substr(i)})));
      if(!err.empty())
      {
        err = "split at " + std::to_string(i) + " [..." + Visible(stream.substr(i > 24 ? i - 24 : 0, i > 24 ? 24 : i)) +
              " | " + Visible(stream.substr(i, 24)) + "...]: " + err;
      }
    }
    ReportTest(name + " every 2-way split (" + std::to_string(stream.size() - 1) + ")", err.empty(), err);
  }
  // (c) byte by byte
  {
    std::vector<std::string> chunks;
    for(char c : stream) chunks.push_back(std::string(1, c));
    std::string err = CompareModels(want, ModelFromEvents(RunChunks(chunks)));
    ReportTest(name + " byte-by-byte", err.empty(), err);
  }
}

static void CheckScenario(const std::string& name, const std::vector<std::string>& messages,
                          const std::string& prefix = "", bool allSplits = true)
{
  Model want = ModelFromSource(messages);
  if(!want.error.empty())
  {
    ReportTest(name + " (source)", false, want.error);
    return;
  }
  const char* seps[] = {"", "\n", "\r\n"};
  const char* sepNames[] = {"concat", "LF", "CRLF"};
  for(int s = 0; s < 3; ++s)
  {
    std::string stream;
    for(const auto& msg : messages) stream += prefix + msg + seps[s];
    CheckStream(name + " [" + sepNames[s] + "]", stream, want, allSplits);
  }
}

// ========================================================================
// Scenarios
// ========================================================================

static const std::string kCreate = R"({"version":"v0.9","createSurface":{"surfaceId":"s","catalogId":"https://a2ui.org/specification/v0_9/basic_catalog.json"}})";
static const std::string kDelete = R"({"version":"v0.9","deleteSurface":{"surfaceId":"s"}})";

// #1: chunks ending after "]" / "]}" of updateComponents, followed by more messages.
static void TestJsonlFraming()
{
  std::cout << "\n=== Test: message framing / whitespace between messages (#1) ===" << std::endl;
  CheckScenario("framing", {
    kCreate,
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Column","children":["a"]},{"id":"a","component":"Image","url":"x.png"}]}})",
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"b","component":"Divider"}]}})",
    kDelete,
  });
  CheckScenario("pretty-printed messages", {
    "{\n  \"version\": \"v0.9\",\n  \"createSurface\": {\n    \"surfaceId\": \"s\",\n    \"catalogId\": \"c\"\n  }\n}",
    "{ \"version\" : \"v0.9\" , \"updateComponents\" : { \"surfaceId\" : \"s\" , \"components\" : [ { \"id\" : \"root\" , \"component\" : \"Divider\" } ] } }",
    "\t{\"version\":\"v0.9\",\"deleteSurface\":{\"surfaceId\":\"s\"}} ",
  });
  // An extra member after "components" must not swallow the next message.
  CheckScenario("extra key after components", {
    kCreate,
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Divider"}],"extra":{"k":[1,"]}"]}}})",
    kDelete,
  });
}

// #2: garbage that cannot start a message is discarded.
static void TestGarbagePrefix()
{
  std::cout << "\n=== Test: garbage prefix recovery (#2) ===" << std::endl;
  CheckScenario("SSE 'data: ' prefix", {kCreate, R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/a","value":1}})", kDelete}, "data: ");
  CheckScenario("stray tokens before message", {kCreate, kDelete}, "]}, ");
}

// #2: buffer overflow resets cleanly instead of splicing broken JSON.
static void TestBufferOverflow()
{
  std::cout << "\n=== Test: buffer overflow (#2) ===" << std::endl;
  std::vector<std::string> chunks;
  chunks.push_back(R"({"version":"v0.9","createSurface":{"surfaceId":"big","catalogId":")");
  std::string mb(1024 * 1024, 'a');
  for(int i = 0; i < 11; ++i) chunks.push_back(mb);
  chunks.push_back("\"}}\n" + kCreate + "\n" + kDelete + "\n");
  std::vector<Event> events = RunChunks(chunks);
  bool sawBig = false;
  bool allValid = true;
  for(const auto& e : events)
  {
    json j = json::parse(e.data, nullptr, false);
    if(j.is_discarded()) allValid = false;
    if(e.data.find("\"big\"") != std::string::npos) sawBig = true;
  }
  Model want = ModelFromSource({kCreate, kDelete});
  std::string err = CompareModels(want, ModelFromEvents(events));
  ReportTest("overflowing message is dropped, not spliced", !sawBig && allValid,
             sawBig ? "a truncated 'big' createSurface was emitted" : "invalid JSON emitted");
  ReportTest("messages after overflow are parsed", err.empty(), err);
}

// #3: streamed Text / Markdown content must never end inside an escape or UTF-8 sequence.
// Escapes are assembled from kBs so this file never contains a literal backslash-u sequence.
static const std::string kBs(1, '\\');
static const std::string kTrickyText =
  "caf" + kBs + "u00e9 " + kBs + "ud83d" + kBs + "ude00 line" + kBs + "nbreak " + kBs + "\"quoted" + kBs + "\" back" +
  kBs + kBs + "slash " + kBs + "/ tab" + kBs + "t " + kBs + "u00" + "41" + kBs + "u0000 raw é 한글 😀 end";

static void TestStreamedTextEscapes()
{
  std::cout << "\n=== Test: escapes / surrogates / UTF-8 in streamed text (#3) ===" << std::endl;
  CheckScenario("inline Text + Markdown", {
    kCreate,
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Column","children":["t","m"]},)"
    R"({"id":"t","component":"Text","text":")" + kTrickyText + R"(","variant":"h2"},)"
    R"({"id":"m","component":"Markdown","content":")" + kTrickyText + R"(","extra":{"k":1}}]}})",
    kDelete,
  });
}

// #3 + #5: data model streaming of bound Text / Markdown fields and nested values.
static void TestDataModelStreaming()
{
  std::cout << "\n=== Test: updateDataModel streaming invariant (#3, #5) ===" << std::endl;
  const std::string comps =
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Column","children":["t","m","l"]},)"
    R"({"id":"t","component":"Text","text":{"path":"/msg"}},)"
    R"({"id":"m","component":"Markdown","content":{"path":"/md"}},)"
    R"({"id":"l","component":"Text","text":{"path":"/user/name"}}]}})";
  CheckScenario("nested value with streamed fields", {
    kCreate, comps,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"msg":")" + kTrickyText + R"(","md":")" + kTrickyText +
      R"(","items":[],"user":{"name":"Kim )" + kBs + R"(u00e9","tags":["a","b"],"empty":{},"deep":[[1,2],[]]},"a/b":1,"t~x":2,"n":-1.5e3,"ok":true}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/items","value":[1,2,{"x":"y"}]}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/items","value":[]}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","value":{"name":"after-path"},"path":"/user"}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/msg","value":")" + kTrickyText + R"("}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/user/name","value":"x\"y"}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/user","value":{"name":"n","list":[{"a":1},{"b":[true,false]}]}}})",
    kDelete,
  });
  CheckScenario("root replace keeps nothing stale", {
    kCreate,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"old":1,"keep":{"x":1}}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"keep":{"y":2},"arr":[{"k":"v"},[]]}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/keep","value":{"z":3}}})",
  });
  CheckScenario("path-less updateDataModel", {
    kCreate,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","value":{"a":{"b":[1,2]},"c":"d"}}})",
  });
}

// The renderer deletes on "value":null and addresses keys with unescaped JSON-pointer
// segments, so containers holding nulls or such keys must not be split member by member.
static void TestNullsAndUnusualKeys()
{
  std::cout << "\n=== Test: nulls and unusual keys in streamed data model values ===" << std::endl;
  CheckScenario("null members", {
    kCreate,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"keep":1,"x":{"y":2}}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/x","value":[1,null,3,{"a":4}]}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/o","value":{"k0":null,"k1":"v","k2":[null]}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/n","value":{"a":{"b":[1,{"c":null,"d":2}],"e":"f"},"g":[[null],[1]]}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/keep","value":null}})",
  });
  CheckScenario("empty, slash, tilde and numeric keys", {
    kCreate,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"keep":1,"x":{"y":2}}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/x","value":{"":{"k":1},"b":2}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":{"":1},"s/l":{"t~x":[1]},"7":{"q":"r"}}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/a","value":{"p":{"q":{"":"deep"}}}}})",
  });
  // The renderer trims path segments, so " a" written per member would land on "a"
  CheckScenario("keys with leading or trailing whitespace", {
    kCreate,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{" a":{"b":1,"c":[1,2]},"d ":{"e":2},"a":0}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/x","value":{"y":{" z ":[{"k":1}]},"w":1}}})",
  });
}

static std::string Nested(size_t depth, const std::string& leaf)
{
  std::string v;
  v.reserve(depth * 7 + leaf.size());
  for(size_t i = 0; i < depth; ++i) v += (i % 2) ? "[" : "{\"a\":";
  v += leaf;
  for(size_t i = depth; i-- > 0;) v += (i % 2) ? "]" : "}";
  return v;
}

// Containers deeper than the streaming depth cap are sent whole, so output stays linear even
// for absurdly deep values, and nothing in the scan path recurses.
static void TestDeepNesting()
{
  std::cout << "\n=== Test: deeply nested data model values ===" << std::endl;
  CheckScenario("nesting beyond the streaming depth cap", {
    kCreate,
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"deep":)" + Nested(40, "\"x\"") +
      R"(,"after":1,"deeper":)" + Nested(70, "[1,2]") + "}}}",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/deep","value":)" + Nested(35, "{}") + "}}",
  });

  // Deep, but still within what the renderer's own JSON parser handles unsplit
  {
    std::string msg = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"d":)" + Nested(5000, "7") + "}}}";
    Model want = ModelFromSource({kCreate, msg});
    std::string stream = kCreate + "\n" + msg + "\n";
    std::string err = want.error.empty() ? CompareModels(want, ModelFromEvents(RunChunks(Chunk(stream, 16)))) : want.error;
    ReportTest("5000 levels in 16 B chunks, same renderer data model as unsplit", err.empty(), err);
    err = want.error.empty() ? CompareModels(want, ModelFromEvents(RunChunks(Chunk(stream, 1)))) : want.error;
    ReportTest("5000 levels byte by byte, same renderer data model as unsplit", err.empty(), err);
  }

  // Far deeper than any parser here builds a tree for (the renderer itself cannot load this
  // unsplit): only time, output size and event validity are checked
  {
    std::string stream = kCreate + "\n" + R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"d":)" +
                         Nested(100000, "7") + "}}}\n";
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Event> events = RunChunks(Chunk(stream, 16));
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    size_t outBytes = 0;
    bool valid = true;
    for(const auto& e : events)
    {
      outBytes += e.data.size();
      valid = valid && json::accept(e.data);
    }
    ReportTest("100000 levels in 16 B chunks under 1 s (" + std::to_string(static_cast<int>(ms)) + " ms)", ms < 1000.0);
    ReportTest("100000 levels: output " + std::to_string(outBytes) + " B for " + std::to_string(stream.size()) +
                 " B input (at most 2x + 64 KB), all events valid",
               valid && outBytes <= 2 * stream.size() + 65536);
  }
}

// A malformed updateDataModel must not stall the stream: like the unsplit path it is passed
// through as received, and the messages after it still arrive.
static void TestMalformedValue()
{
  std::cout << "\n=== Test: malformed updateDataModel value (pass-through, no stall) ===" << std::endl;
  const std::vector<std::string> bad = {
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":1,"b":tru}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":[1,NaN]}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":"x\qy","b":2}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":1 "b":2}}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":{"b":nul}}}})",
  };
  const std::string after = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/z","value":1}})";
  for(size_t b = 0; b < bad.size(); ++b)
  {
    std::string stream = kCreate + "\n" + bad[b] + "\n" + after + "\n" + kDelete + "\n";
    auto check = [&](const std::vector<Event>& events) -> std::string {
      int raw = 0, creates = 0, deletes = 0, afters = 0;
      for(const auto& e : events)
      {
        if(e.data == bad[b]) { raw++; continue; }
        if(!json::accept(e.data)) return "invalid event: " + e.data.substr(0, 120);
        if(e.type == "createSurface") creates++;
        if(e.type == "deleteSurface") deletes++;
        if(e.data == after) afters++;
      }
      if(raw != 1) return "malformed message delivered " + std::to_string(raw) + " times";
      if(creates != 1 || deletes != 1 || afters != 1) return "messages after it were lost";
      return "";
    };
    std::string name = "malformed value #" + std::to_string(b + 1);
    std::string err = check(RunChunks({stream}));
    ReportTest(name + " unsplit", err.empty(), err);
    for(size_t i = 1; i < stream.size() && err.empty(); ++i)
    {
      err = check(RunChunks({stream.substr(0, i), stream.substr(i)}));
      if(!err.empty()) err = "split at " + std::to_string(i) + ": " + err;
    }
    ReportTest(name + " every 2-way split", err.empty(), err);
    err = check(RunChunks(Chunk(stream, 1)));
    ReportTest(name + " byte-by-byte", err.empty(), err);
  }
}

// #4: updateComponents without "version" (and with version after the payload).
static void TestVersionHandling()
{
  std::cout << "\n=== Test: version handling (#4, #5) ===" << std::endl;
  CheckScenario("no version anywhere", {
    R"({"createSurface":{"surfaceId":"s","catalogId":"c"}})",
    R"({"updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Text","text":"hello world"}]}})",
    R"({"updateDataModel":{"surfaceId":"s","path":"/","value":{"a":{"b":"c"},"l":[1]}}})",
    R"({"deleteSurface":{"surfaceId":"s"}})",
  });
  CheckScenario("version after payload", {
    R"({"createSurface":{"surfaceId":"s","catalogId":"c"},"version":"v0.9"})",
    R"({"updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Text","text":"late version"}]},"version":"v0.9"})",
    R"({"updateDataModel":{"surfaceId":"s","path":"/","value":{"a":{"b":"c"}}},"version":"v0.9"})",
  });
  CheckScenario("surfaceId after components", {
    kCreate,
    R"({"version":"v0.9","updateComponents":{"components":[{"id":"root","component":"Divider","surfaceId":"other"}],"surfaceId":"s"}})",
  });
}

// #6: the message type comes from the top-level key, not from any substring.
static void TestTypeDetection()
{
  std::cout << "\n=== Test: message type detection (#6) ===" << std::endl;
  A2ui::ProtocolStreamExtractor ex;
  using ET = A2ui::ProtocolStreamExtractor::EventType;
  ReportTest("detectEventType: value string 'createSurface'",
             ex.detectEventType(R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/x","value":"createSurface"}})") == ET::UpdateDataModel);
  ReportTest("detectEventType: catalogId mentions updateComponents",
             ex.detectEventType(R"({"createSurface":{"surfaceId":"s","catalogId":"\"updateComponents\""}})") == ET::CreateSurface);
  ReportTest("detectEventType: partial message",
             ex.detectEventType(R"({"version":"v0.9","deleteSurface":{"surf)") == ET::DeleteSurface);
  ReportTest("detectEventType: unknown top-level key",
             ex.detectEventType(R"({"other":{"createSurface":{}}})") == ET::Unknown);
  CheckScenario("values that name other message types", {
    R"({"version":"v0.9","createSurface":{"surfaceId":"s","catalogId":"\"updateComponents\""}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/last","value":"createSurface"}})",
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/deleteSurface","value":{"\"deleteSurface\"":"updateDataModel"}}})",
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"root","component":"Text","text":"\"deleteSurface\""}]}})",
    kDelete,
  });
}

// The fixes must not degrade streaming into "wait for the whole message": the
// convergence checks above would still pass, so check progressive delivery directly.
static void TestProgressiveDelivery()
{
  std::cout << "\n=== Test: progressive delivery is kept ===" << std::endl;
  auto count = [](const std::vector<Event>& events, const std::string& type, const std::string& needle) {
    int n = 0;
    for(const auto& e : events)
    {
      if(e.type == type && e.data.find(needle) != std::string::npos) n++;
    }
    return n;
  };
  auto bytes = [](const std::string& s) {
    std::vector<std::string> chunks;
    for(char c : s) chunks.push_back(std::string(1, c));
    return chunks;
  };

  // Components are delivered one by one before their message is complete.
  {
    CollectingSink sink;
    A2ui::StreamingContentParser parser(&sink);
    parser.processDataBeginning();
    parser.processDataAssembling(R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"a","component":"Divider"},{"id":"b",)");
    ReportTest("first component emitted before the message completes",
               sink.events.size() == 1 && sink.events[0].data.find("\"id\":\"a\"") != std::string::npos);
    parser.processDataEnding();
  }

  // Inline Text / Markdown content is streamed as textChunk / appendContent.
  std::string inlineText =
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"t","component":"Text","text":")" + kTrickyText +
    R"("},{"id":"m","component":"Markdown","content":")" + kTrickyText + R"("}]}})" "\n";
  std::vector<Event> ev = RunChunks(bytes(inlineText));
  ReportTest("inline Text streamed as textChunk", count(ev, "updateComponents", "\"textChunk\"") > 10);
  ReportTest("inline Markdown streamed as appendContent", count(ev, "updateComponents", "\"appendContent\"") > 10);

  // Data model: bound string fields are appended, nested values are sent before the message ends.
  std::string dm =
    R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"t","component":"Text","text":{"path":"/msg"}}]}})" "\n"
    R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"msg":")" + kTrickyText +
    R"(","list":[{"a":1},{"b":2}]}}})" "\n";
  ev = RunChunks(bytes(dm));
  ReportTest("bound data model string streamed as appendDataModel", count(ev, "appendDataModel", "\"/msg\"") > 10);
  ReportTest("nested data model value streamed member by member", count(ev, "updateDataModel", "\"/list/1\"") == 1);
  ReportTest("synthesized data model events keep version", count(ev, "updateDataModel", "\"version\":\"v0.9\"") == count(ev, "updateDataModel", ""));
}

// Total work must stay linear in the input, also for tiny chunks (LLM token streams can deliver a
// few bytes at a time): a 1 MB message in 16-byte chunks is parsed in tens of milliseconds, while
// rescanning the pending bytes on every chunk takes many seconds.
static void TestLinearTime()
{
  std::cout << "\n=== Test: parsing time is linear in the input ===" << std::endl;
  const size_t n = 1024 * 1024;
  const std::string big(n, 'a');
  const std::string huge(4 * n, 'a'); // Where a rescan per chunk is cheaper per byte, use 4 MB
  std::string members;
  for(int i = 0; members.size() < n; ++i) members += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":" + std::to_string(i);
  const std::string dm = R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":)";
  const std::string uc = R"({"version":"v0.9","updateComponents":{"surfaceId":"s","components":[)";
  struct Case { const char* name; std::string stream; };
  std::vector<Case> cases = {
    {"1 MB updateDataModel without version (delivered whole)", R"({"updateDataModel":{"surfaceId":"s","path":"/","value":{)" + members + "}}}\n"},
    {"1 MB streamed updateDataModel string", dm + R"({"z":")" + big + "\"}}}\n"},
    {"1 MB updateDataModel huge key", dm + "{\"" + big + "\":1}}}\n"},
    {"1 MB updateDataModel big member before value", R"({"version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","extra":")" + big + R"(","value":{"a":1}}})" "\n"},
    {"1 MB big unknown member before the message key", R"({"meta":")" + big + R"(","version":"v0.9","updateDataModel":{"surfaceId":"s","path":"/","value":{"a":1}}})" "\n"},
    {"1 MB big unknown member before updateComponents", R"({"meta":")" + big + R"(","version":"v0.9","updateComponents":{"surfaceId":"s","components":[{"id":"a","component":"Divider"}]}})" "\n"},
    {"1 MB message without a known key", R"({"junk":")" + big + "\"}\n" + kCreate + "\n"},
    {"1 MB createSurface", R"({"version":"v0.9","createSurface":{"surfaceId":"s","catalogId":")" + big + "\"}}\n"},
    {"1 MB long Text component", uc + R"({"id":"t","component":"Text","text":")" + big + "\"}]}}\n"},
    {"4 MB member before a Text component's text", uc + R"({"id":"t","component":"Text","description":")" + huge + R"(","text":"x"}]}})" "\n"},
    {"4 MB Markdown without content", uc + R"({"id":"m","component":"Markdown","text":")" + huge + "\"}]}}\n"},
    {"1 MB long Image component", uc + R"({"id":"i","component":"Image","url":")" + big + "\"}]}}\n"},
  };
  for(const auto& c : cases)
  {
    std::vector<std::string> chunks = Chunk(c.stream, 16);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Event> events = RunChunks(chunks);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    // Generous bound: linear parsing needs well under 200 ms here, a rescan per chunk 5+ s.
    ReportTest(std::string(c.name) + " in 16 B chunks under 1 s (" + std::to_string(static_cast<int>(ms)) + " ms)",
               ms < 1000.0 && !events.empty());
  }
}

// ========================================================================
// Repo samples (JSON arrays of messages) streamed as JSONL
// ========================================================================

static bool ReadFile(const std::string& path, std::string& out)
{
  std::ifstream ifs(path, std::ios::binary);
  if(!ifs.is_open()) return false;
  std::ostringstream ss;
  ss << ifs.rdbuf();
  out = ss.str();
  return true;
}

static std::vector<std::string> ListDir(const std::string& dir, const std::string& ext)
{
  std::vector<std::string> files;
  DIR* d = opendir(dir.c_str());
  if(!d) return files;
  while(dirent* e = readdir(d))
  {
    std::string n = e->d_name;
    if(n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) files.push_back(dir + "/" + n);
  }
  closedir(d);
  std::sort(files.begin(), files.end());
  return files;
}

static void TestRepoSamples(const std::string& root)
{
  std::cout << "\n=== Test: repo samples streamed as JSONL ===" << std::endl;
  std::vector<std::string> files = ListDir(root + "/examples/samples", ".json");
  std::vector<std::string> screens = ListDir(root + "/examples/gallery-demo/screens", ".json");
  files.insert(files.end(), screens.begin(), screens.end());
  std::vector<std::string> jsonl = ListDir(root + "/test/e2e", ".jsonl");
  files.insert(files.end(), jsonl.begin(), jsonl.end());
  ReportTest("found sample files under " + root, files.size() >= 17, std::to_string(files.size()) + " files");
  gLenient = true; // e2e streams rely on the renderer creating intermediate containers

  for(const auto& f : files)
  {
    std::string raw;
    if(!ReadFile(f, raw))
    {
      ReportTest(f, false, "cannot read");
      continue;
    }
    std::vector<json> msgs;
    json parsed = json::parse(raw, nullptr, false);
    if(!parsed.is_discarded() && parsed.is_array())
    {
      for(const auto& m : parsed) msgs.push_back(m);
    }
    else
    {
      std::istringstream lines(raw);
      std::string line;
      while(std::getline(lines, line))
      {
        if(line.find_first_not_of(" \t\r") == std::string::npos) continue;
        json m = json::parse(line, nullptr, false);
        if(!m.is_discarded()) msgs.push_back(m);
      }
    }
    std::vector<std::string> compact, ascii, pretty;
    for(const auto& m : msgs)
    {
      compact.push_back(m.dump());
      ascii.push_back(m.dump(-1, ' ', true));
      pretty.push_back(m.dump(2));
    }
    Model want = ModelFromSource(compact);
    std::string name = f.substr(f.find_last_of('/') + 1);
    if(!want.error.empty())
    {
      ReportTest(name + " (source)", false, want.error);
      continue;
    }
    auto jsonl = [](const std::vector<std::string>& v) {
      std::string s;
      for(const auto& x : v) s += x + "\n";
      return s;
    };
    struct Run { const char* label; std::string stream; size_t chunk; };
    std::vector<Run> runs = {
      {"JSONL 64B", jsonl(compact), 64},
      {"JSONL 1B", jsonl(compact), 1},
      {"JSONL ascii-escaped 1B", jsonl(ascii), 1},
      {"pretty 64B", jsonl(pretty), 64},
    };
    for(const auto& r : runs)
    {
      std::string err = CompareModels(want, ModelFromEvents(RunChunks(Chunk(r.stream, r.chunk))));
      ReportTest(name + " " + r.label, err.empty(), err);
    }
  }
}

// ========================================================================
// Main
// ========================================================================

int main(int argc, char** argv)
{
  std::string root = "..";
  bool verbose = false;
  for(int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    if(a == "-v") verbose = true;
    else root = a;
  }
  if(!verbose)
  {
    // The parser logs to stderr; the byte-by-byte runs would flood the output.
    if(!std::freopen("/dev/null", "w", stderr)) std::cout << "(could not silence stderr)" << std::endl;
  }

  std::cout << "========================================" << std::endl;
  std::cout << "  a2ui-dali stream parser test" << std::endl;
  std::cout << "  repo root: " << root << std::endl;
  std::cout << "========================================" << std::endl;

  TestJsonlFraming();
  TestGarbagePrefix();
  TestBufferOverflow();
  TestStreamedTextEscapes();
  TestDataModelStreaming();
  TestVersionHandling();
  TestTypeDetection();
  TestProgressiveDelivery();
  TestNullsAndUnusualKeys();
  TestMalformedValue();
  TestDeepNesting();
  TestLinearTime();
  TestRepoSamples(root);

  std::cout << "\n========================================" << std::endl;
  std::cout << "  RESULTS: " << gPassed << "/" << gTotal << " passed";
  if(gFailed > 0) std::cout << " (" << gFailed << " FAILED)";
  std::cout << std::endl;
  if(gFailed > 0)
  {
    std::cout << "\n  Failed tests:" << std::endl;
    for(const auto& f : gFailures) std::cout << "    - " << f.substr(0, 400) << std::endl;
  }
  std::cout << "========================================" << std::endl;
  return gFailed > 0 ? 1 : 0;
}
