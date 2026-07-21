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

#include "data-model.h"
#include <dali/integration-api/debug.h>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cerrno>
#include <algorithm>

using Dali::Ui::Integration::JsonParser;
using Dali::Ui::Integration::TreeNode;

namespace A2ui
{

// Format a float as the shortest decimal that round-trips back to the same float32 — the
// way JS prints a number on the web. Avoids both the default ostream's 6-significant-figure
// truncation (12458.32 → "12458.3") and trailing floating-point noise (→ "12458.3203125").
std::string DataModel::FormatFloatToken(float f)
{
  if(std::isnan(f) || std::isinf(f)) return "0";
  // Whole numbers: plain fixed notation (avoids %g's "1e+06" for e.g. 1000000.0).
  if(std::fabs(f) < 1e15f && f == std::floor(f))
  {
    char w[32];
    std::snprintf(w, sizeof(w), "%.0f", static_cast<double>(f));
    return std::string(w);
  }
  // Shortest decimal that round-trips to within one ULP. The one-ULP tolerance undoes the
  // last-bit rounding of the dali JSON float parser, so a value it stored as 0.45000005
  // prints as the "0.45" it was authored as — while a genuinely distinct float keeps its
  // own shortest form.
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(f));
  float ulp = std::fmax(std::nextafterf(f, HUGE_VALF) - f, f - std::nextafterf(f, -HUGE_VALF));
  for(int prec = 1; prec <= 9; ++prec)
  {
    char cand[64];
    std::snprintf(cand, sizeof(cand), "%.*g", prec, static_cast<double>(f));
    if(std::fabs(std::strtof(cand, nullptr) - f) <= ulp) { std::strcpy(buf, cand); break; }
  }
  return std::string(buf);
}

// The dali JSON parser stores every integer in a 32-bit field, so a value beyond ±2^31
// (e.g. a 850000000000 market cap) is silently mangled at parse time. We can't widen the
// shared parser, so before parsing we quote integer literals that overflow int32 — the
// parser then keeps them verbatim as strings (full precision), and the consumers that read
// them (formatCurrency / GetFloat) accept a numeric string. Floats and in-range integers
// are untouched. String contents are skipped so digits inside text are never rewritten.
std::string DataModel::WidenLargeIntegers(const std::string& json)
{
  std::string out;
  out.reserve(json.size() + 16);
  bool inStr = false;
  for(std::size_t i = 0; i < json.size();)
  {
    char c = json[i];
    if(inStr)
    {
      out += c;
      if(c == '\\' && i + 1 < json.size()) { out += json[i + 1]; i += 2; continue; }
      if(c == '"') inStr = false;
      ++i; continue;
    }
    if(c == '"') { inStr = true; out += c; ++i; continue; }

    // A numeric token in value position: optional '-' then a digit run. (Object keys are
    // always quoted, so an unquoted number here is always a value.)
    bool startsNum = (c >= '0' && c <= '9') ||
                     (c == '-' && i + 1 < json.size() && json[i + 1] >= '0' && json[i + 1] <= '9');
    if(startsNum)
    {
      std::size_t j = i;
      if(json[j] == '-') ++j;
      bool isFloat = false;
      while(j < json.size())
      {
        char d = json[j];
        if(d >= '0' && d <= '9') { ++j; }
        else if(d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') { isFloat = true; ++j; }
        else break;
      }
      std::string tok = json.substr(i, j - i);
      bool overflow = false;
      if(!isFloat)
      {
        errno = 0;
        long long v = std::strtoll(tok.c_str(), nullptr, 10);
        if(errno == ERANGE || v > 2147483647LL || v < -2147483648LL) overflow = true;
      }
      if(overflow) { out += '"'; out += tok; out += '"'; }
      else         { out += tok; }
      i = j; continue;
    }
    out += c; ++i;
  }
  return out;
}

// ========================================================================
// Data mutation
// ========================================================================

bool DataModel::SetData(const std::string& path, const std::string& rawJson)
{
  // Preserve integers beyond int32 (the shared dali parser would mangle them) before any
  // parse, on both the whole-document and merge paths.
  std::string jsonString = WidenLargeIntegers(rawJson);
  if(path == "/" || path.empty())
  {
    mJsonString = jsonString;
    mParser = JsonParser::New();
    if(!mParser.Parse(mJsonString))
    {
      DALI_LOG_ERROR("[A2UI] DataModel: JSON parse error: %s\n",
                     mParser.GetErrorDescription().c_str());
      mParser.Reset();
      return false;
    }
    NotifyObservers(path);
    return true;
  }

  if(MergeAtPath(path, jsonString))
  {
    NotifyObservers(path);
    return true;
  }
  return false;
}

bool DataModel::SetDataFromNode(const std::string& path, const TreeNode& value)
{
  std::string valueJson = TreeNodeToJson(value);

  if(path == "/" || path.empty())
  {
    return SetData("/", valueJson);
  }

  return SetData(path, valueJson);
}

bool DataModel::SetValue(const std::string& path, const std::string& value)
{
  // Wrap string value in quotes for JSON
  std::ostringstream oss;
  oss << "\"";
  EscapeJsonString(oss, value.c_str());
  oss << "\"";
  return SetData(path, oss.str());
}

bool DataModel::SetBoolValue(const std::string& path, bool value)
{
  return SetData(path, value ? "true" : "false");
}

bool DataModel::SetFloatValue(const std::string& path, float value)
{
  return SetData(path, FormatFloatToken(value));
}

// ========================================================================
// Data reading
// ========================================================================

const TreeNode* DataModel::ResolvePath(const std::string& path) const
{
  if(!mParser)
  {
    return nullptr;
  }

  const TreeNode* root = mParser.GetRoot();
  if(!root)
  {
    return nullptr;
  }

  if(path == "/" || path.empty())
  {
    return root;
  }

  const TreeNode* current = root;
  size_t start = 1; // Skip leading '/'

  while(start < path.size())
  {
    size_t end = path.find('/', start);
    std::string segment;
    if(end == std::string::npos)
    {
      segment = path.substr(start);
      start = path.size();
    }
    else
    {
      segment = path.substr(start, end - start);
      start = end + 1;
    }

    if(segment.empty())
    {
      continue;
    }

    // Try as object key first
    const TreeNode* child = current->Find(segment.c_str());
    if(child)
    {
      current = child;
      continue;
    }

    // Try as array index
    if(current->GetType() == TreeNode::ARRAY)
    {
      char* endPtr = nullptr;
      long idx = strtol(segment.c_str(), &endPtr, 10);
      if(endPtr && *endPtr == '\0' && idx >= 0)
      {
        long count = 0;
        for(auto it = current->CBegin(); it != current->CEnd(); ++it)
        {
          if(count == idx)
          {
            current = &(*it).second;
            break;
          }
          count++;
        }
        if(count == idx)
        {
          continue;
        }
      }
    }

    return nullptr;
  }

  return current;
}

std::string DataModel::GetString(const std::string& path) const
{
  const TreeNode* node = ResolvePath(path);
  if(!node)
  {
    return "";
  }

  switch(node->GetType())
  {
    case TreeNode::STRING:
      return node->GetString();
    case TreeNode::INTEGER:
      return std::to_string(node->GetInteger());
    case TreeNode::FLOAT:
      return FormatFloatToken(node->GetFloat());
    case TreeNode::BOOLEAN:
      return node->GetBoolean() ? "true" : "false";
    default:
      return "";
  }
}

float DataModel::GetFloat(const std::string& path, float fallback) const
{
  const TreeNode* node = ResolvePath(path);
  if(!node)
  {
    return fallback;
  }

  if(node->GetType() == TreeNode::FLOAT)
    return node->GetFloat();
  if(node->GetType() == TreeNode::INTEGER)
    return static_cast<float>(node->GetInteger());
  if(node->GetType() == TreeNode::STRING)
  {
    // A numeric value may arrive as a string (a >int32 integer is stored quoted to keep its
    // precision); parse it so slider/number consumers still see a float.
    const char* s = node->GetString();
    if(s && *s)
    {
      char* end = nullptr;
      float v = std::strtof(s, &end);
      if(end != s) return v;
    }
  }

  return fallback;
}

bool DataModel::GetBool(const std::string& path, bool fallback) const
{
  const TreeNode* node = ResolvePath(path);
  if(!node)
  {
    return fallback;
  }

  if(node->GetType() == TreeNode::BOOLEAN)
    return node->GetBoolean();

  // Also accept string "true"/"false"
  if(node->GetType() == TreeNode::STRING)
  {
    const char* str = node->GetString();
    if(strcmp(str, "true") == 0) return true;
    if(strcmp(str, "false") == 0) return false;
  }

  return fallback;
}

std::string DataModel::Serialize() const
{
  if(!mParser || !mParser.GetRoot())
  {
    return "{}";
  }
  return TreeNodeToJson(*mParser.GetRoot());
}

// ========================================================================
// Observer pattern
// ========================================================================

uint32_t DataModel::Watch(const std::string& path, DataChangeCallback cb)
{
  uint32_t id = mNextObserverId++;
  if(id == 0) id = mNextObserverId++; // skip 0

  if(mNotifying)
  {
    // Defer addition until notification loop completes
    mPendingAdds.push_back({id, path, std::move(cb)});
  }
  else
  {
    mObservers.push_back({id, path, std::move(cb)});
  }
  return id;
}

void DataModel::Unwatch(uint32_t observerId)
{
  if(mNotifying)
  {
    // Defer removal until notification loop completes
    mPendingRemoves.push_back(observerId);
  }
  else
  {
    mObservers.erase(
      std::remove_if(mObservers.begin(), mObservers.end(),
        [observerId](const Observer& o) { return o.id == observerId; }),
      mObservers.end());
  }
}

static bool IsHierarchicalMatch(const std::string& a, const std::string& b)
{
  // Check if 'a' is a hierarchical prefix of 'b'.
  // "/contact" matches "/contact/name" but NOT "/contactName"
  if(b.find(a) != 0) return false;
  if(b.size() == a.size()) return true; // exact match
  return b[a.size()] == '/'; // next char must be separator
}

void DataModel::NotifyObservers(const std::string& changedPath)
{
  if(mNotifying)
  {
    return; // prevent re-entrancy
  }
  mNotifying = true;

  // Use index-based iteration to be safe if observers are modified during callbacks
  for(size_t i = 0; i < mObservers.size(); ++i)
  {
    const auto& obs = mObservers[i];
    bool notify = false;

    if(changedPath == "/" || changedPath.empty())
    {
      notify = true;
    }
    else if(obs.path == changedPath)
    {
      notify = true;
    }
    else if(IsHierarchicalMatch(obs.path, changedPath))
    {
      notify = true; // changed "/contact/name", observer "/contact"
    }
    else if(IsHierarchicalMatch(changedPath, obs.path))
    {
      notify = true; // changed "/contact", observer "/contact/name"
    }

    if(notify)
    {
      std::string newValue = GetString(obs.path);
      obs.callback(obs.path, newValue);
    }
  }

  mNotifying = false;

  // Apply deferred modifications
  for(auto id : mPendingRemoves)
  {
    mObservers.erase(
      std::remove_if(mObservers.begin(), mObservers.end(),
        [id](const Observer& o) { return o.id == id; }),
      mObservers.end());
  }
  mPendingRemoves.clear();

  for(auto& obs : mPendingAdds)
  {
    mObservers.push_back(std::move(obs));
  }
  mPendingAdds.clear();
}

void DataModel::Clear()
{
  if(mParser)
  {
    mParser.Reset();
  }
  mJsonString.clear();
  mObservers.clear();
  mPendingAdds.clear();
  mPendingRemoves.clear();
  mNextObserverId = 1;
}

void DataModel::ClearObservers()
{
  mObservers.clear();
  mPendingAdds.clear();
  mPendingRemoves.clear();
}

// ========================================================================
// Internal merge
// ========================================================================

bool DataModel::MergeAtPath(const std::string& path, const std::string& valueJson)
{
  if(!mParser || !mParser.GetRoot())
  {
    // No existing data - build nested structure from path
    // e.g., path="/contact/firstName", value="\"John\"" -> {"contact":{"firstName":"John"}}
    std::vector<std::string> segments;
    size_t start = 1;
    while(start < path.size())
    {
      size_t end = path.find('/', start);
      if(end == std::string::npos)
      {
        segments.push_back(path.substr(start));
        break;
      }
      segments.push_back(path.substr(start, end - start));
      start = end + 1;
    }

    if(segments.empty())
    {
      return false;
    }

    // Build nested JSON from inside out
    std::string json = valueJson;
    for(int i = static_cast<int>(segments.size()) - 1; i >= 0; --i)
    {
      json = "{\"" + segments[i] + "\":" + json + "}";
    }

    mJsonString = json;
    mParser = JsonParser::New();
    if(!mParser.Parse(mJsonString))
    {
      DALI_LOG_ERROR("[A2UI] DataModel: MergeAtPath parse error\n");
      mParser.Reset();
      return false;
    }
    return true;
  }

  // Existing data: rebuild JSON with the path updated
  const TreeNode* root = mParser.GetRoot();

  // Parse path segments
  std::vector<std::string> segments;
  size_t start = 1;
  while(start < path.size())
  {
    size_t end = path.find('/', start);
    if(end == std::string::npos)
    {
      segments.push_back(path.substr(start));
      break;
    }
    segments.push_back(path.substr(start, end - start));
    start = end + 1;
  }

  if(segments.empty())
  {
    return false;
  }

  // For single-level paths, use the optimized approach
  if(segments.size() == 1)
  {
    const std::string& key = segments[0];
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    bool found = false;

    if(root->GetType() == TreeNode::OBJECT)
    {
      for(auto it = root->CBegin(); it != root->CEnd(); ++it)
      {
        if(!first) oss << ",";
        first = false;

        const char* name = (*it).first;
        if(name && strcmp(name, key.c_str()) == 0)
        {
          oss << "\"";
          EscapeJsonString(oss, name);
          oss << "\":" << valueJson;
          found = true;
        }
        else if(name)
        {
          oss << "\"";
          EscapeJsonString(oss, name);
          oss << "\":" << TreeNodeToJson((*it).second);
        }
      }
    }

    if(!found)
    {
      if(!first) oss << ",";
      oss << "\"" << key << "\":" << valueJson;
    }

    oss << "}";

    mJsonString = oss.str();
    mParser = JsonParser::New();
    if(!mParser.Parse(mJsonString))
    {
      DALI_LOG_ERROR("[A2UI] DataModel: MergeAtPath re-parse error\n");
      mParser.Reset();
      return false;
    }
    return true;
  }

  // Multi-level path: serialize the whole tree, updating the nested value
  // Strategy: serialize root, but intercept at the first segment level
  // and recursively handle deeper segments
  std::string rootJson = TreeNodeToJson(*root);

  // Re-parse into a working copy, then do recursive merge
  // Approach: build the intermediate objects from the path
  // First, get or create the top-level key
  std::string topKey = segments[0];

  // Build the nested value from segments[1..] wrapping valueJson
  std::string nestedValue = valueJson;
  for(int i = static_cast<int>(segments.size()) - 1; i >= 1; --i)
  {
    // Check if intermediate node exists
    std::string intermediatePath = "/";
    for(int j = 0; j <= i - 1; ++j)
    {
      if(j > 0) intermediatePath += "/";
      intermediatePath += segments[j];
    }
    const TreeNode* intermediateNode = ResolvePath(intermediatePath);

    if(intermediateNode && intermediateNode->GetType() == TreeNode::ARRAY)
    {
      // "/forecast/1/temp" — the parent is a LIST, so segments[i] is an index, not a key.
      // Rewriting it as an object ({"1": …}) would silently turn the array into an object
      // and every data-driven child list bound to it would render nothing.
      long index = -1;
      if(!segments[i].empty() &&
         segments[i].find_first_not_of("0123456789") == std::string::npos)
      {
        index = std::strtol(segments[i].c_str(), nullptr, 10);
      }

      std::ostringstream oss;
      oss << "[";
      bool first = true;
      long at    = 0;
      for(auto it = intermediateNode->CBegin(); it != intermediateNode->CEnd(); ++it, ++at)
      {
        if(!first) oss << ",";
        first = false;
        oss << ((at == index) ? nestedValue : TreeNodeToJson((*it).second));
      }
      if(index >= at) // past the end (or a non-numeric segment) — append rather than drop
      {
        if(!first) oss << ",";
        oss << nestedValue;
      }
      oss << "]";
      nestedValue = oss.str();
    }
    else if(intermediateNode && intermediateNode->GetType() == TreeNode::OBJECT)
    {
      // Merge into existing object
      std::ostringstream oss;
      oss << "{";
      bool first = true;
      bool found = false;

      for(auto it = intermediateNode->CBegin(); it != intermediateNode->CEnd(); ++it)
      {
        if(!first) oss << ",";
        first = false;

        const char* name = (*it).first;
        if(name && strcmp(name, segments[i].c_str()) == 0)
        {
          oss << "\"";
          EscapeJsonString(oss, name);
          oss << "\":" << nestedValue;
          found = true;
        }
        else if(name)
        {
          oss << "\"";
          EscapeJsonString(oss, name);
          oss << "\":" << TreeNodeToJson((*it).second);
        }
      }

      if(!found)
      {
        if(!first) oss << ",";
        oss << "\"" << segments[i] << "\":" << nestedValue;
      }

      oss << "}";
      nestedValue = oss.str();
    }
    else
    {
      // Create new intermediate object
      nestedValue = "{\"" + segments[i] + "\":" + nestedValue + "}";
    }
  }

  // Now merge nestedValue at top-level key
  return MergeAtPath("/" + topKey, nestedValue);
}

// ========================================================================
// Serialization helpers
// ========================================================================

void DataModel::EscapeJsonString(std::ostringstream& oss, const char* str)
{
  while(*str)
  {
    switch(*str)
    {
      case '"':  oss << "\\\""; break;
      case '\\': oss << "\\\\"; break;
      case '\n': oss << "\\n";  break;
      case '\r': oss << "\\r";  break;
      case '\t': oss << "\\t";  break;
      default:   oss << *str;   break;
    }
    ++str;
  }
}

std::string DataModel::TreeNodeToJson(const TreeNode& node)
{
  std::ostringstream oss;

  switch(node.GetType())
  {
    case TreeNode::OBJECT:
    {
      oss << "{";
      bool first = true;
      for(auto it = node.CBegin(); it != node.CEnd(); ++it)
      {
        if(!first) oss << ",";
        first = false;
        if((*it).first)
        {
          oss << "\"";
          EscapeJsonString(oss, (*it).first);
          oss << "\":";
        }
        oss << TreeNodeToJson((*it).second);
      }
      oss << "}";
      break;
    }
    case TreeNode::ARRAY:
    {
      oss << "[";
      bool first = true;
      for(auto it = node.CBegin(); it != node.CEnd(); ++it)
      {
        if(!first) oss << ",";
        first = false;
        oss << TreeNodeToJson((*it).second);
      }
      oss << "]";
      break;
    }
    case TreeNode::STRING:
    {
      oss << "\"";
      EscapeJsonString(oss, node.GetString());
      oss << "\"";
      break;
    }
    case TreeNode::INTEGER:
      oss << node.GetInteger();
      break;
    case TreeNode::FLOAT:
      oss << FormatFloatToken(node.GetFloat());
      break;
    case TreeNode::BOOLEAN:
      oss << (node.GetBoolean() ? "true" : "false");
      break;
    default:
      oss << "null";
      break;
  }

  return oss.str();
}

} // namespace A2ui
