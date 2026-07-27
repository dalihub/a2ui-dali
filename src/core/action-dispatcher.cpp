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

#include "action-dispatcher.h"
#include "a2ui-protocol.h"
#include <sstream>
#include <cstring>
#include <ctime>

using Dali::Ui::Integration::TreeNode;

namespace A2ui
{

namespace
{
void EscapeJsonStr(std::ostringstream& oss, const std::string& str)
{
  for(char c : str)
  {
    switch(c)
    {
      case '"':  oss << "\\\""; break;
      case '\\': oss << "\\\\"; break;
      case '\n': oss << "\\n";  break;
      case '\r': oss << "\\r";  break;
      case '\t': oss << "\\t";  break;
      default:   oss << c;      break;
    }
  }
}

/// ISO 8601 UTC timestamp ("2026-07-27T09:15:03Z") — the format the
/// renderer_to_agent schema requires for `action.timestamp`.
std::string IsoTimestampUtc()
{
  std::time_t now = std::time(nullptr);
  std::tm     utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}
} // anonymous namespace

void ActionDispatcher::Dispatch(const TreeNode& actionNode,
                                const std::string& sourceComponentId,
                                const DataContext& ctx)
{
  // Extract event definition
  const TreeNode* eventNode = actionNode.Find("event");
  if(!eventNode) return;

  const TreeNode* nameNode = eventNode->Find("name");
  if(!nameNode || nameNode->GetType() != TreeNode::STRING) return;

  std::string actionName = nameNode->GetString();

  // Build the renderer-to-agent event envelope. The message key is `action`
  // (`userAction` is the v0.8 spelling); name / surfaceId / sourceComponentId /
  // timestamp / context are all required by the schema.
  std::ostringstream oss;
  oss << "{\"version\":\"" << A2UI_PROTOCOL_VERSION << "\",\"action\":{";
  oss << "\"name\":\""; EscapeJsonStr(oss, actionName); oss << "\"";
  oss << ",\"surfaceId\":\""; EscapeJsonStr(oss, mSurfaceId); oss << "\"";
  oss << ",\"sourceComponentId\":\""; EscapeJsonStr(oss, sourceComponentId); oss << "\"";
  oss << ",\"timestamp\":\"" << IsoTimestampUtc() << "\"";

  // Resolve context bindings. `context` is required, so an action that declares
  // none still emits an empty object.
  const TreeNode* contextNode = eventNode->Find("context");
  if(contextNode && contextNode->GetType() == TreeNode::OBJECT)
  {
    oss << ",\"context\":" << BuildContextJson(*contextNode, ctx);
  }
  else
  {
    oss << ",\"context\":{}";
  }

  // wantResponse
  const TreeNode* wantResponseNode = eventNode->Find("wantResponse");
  if(wantResponseNode && wantResponseNode->GetType() == TreeNode::BOOLEAN &&
     wantResponseNode->GetBoolean())
  {
    oss << ",\"wantResponse\":true";
    // Generate a simple action ID
    static uint32_t sActionCounter = 0;
    oss << ",\"actionId\":\"act_" << (++sActionCounter) << "\"";
  }

  oss << "}}";

  std::string actionJson = oss.str();

  if(mSendCallback)
  {
    mSendCallback(actionJson);
  }
}

std::string ActionDispatcher::BuildContextJson(const TreeNode& contextNode,
                                               const DataContext& ctx) const
{
  std::ostringstream oss;
  oss << "{";
  bool first = true;

  for(auto it = contextNode.CBegin(); it != contextNode.CEnd(); ++it)
  {
    if(!first) oss << ",";
    first = false;

    const char* key = (*it).first;
    if(key)
    {
      oss << "\"" << key << "\":";
      oss << ResolveContextValue((*it).second, ctx);
    }
  }

  oss << "}";
  return oss.str();
}

std::string ActionDispatcher::ResolveContextValue(const TreeNode& node,
                                                  const DataContext& ctx) const
{
  if(node.GetType() == TreeNode::STRING)
  {
    std::ostringstream oss;
    oss << "\"";
    EscapeJsonStr(oss, node.GetString());
    oss << "\"";
    return oss.str();
  }

  if(node.GetType() == TreeNode::OBJECT)
  {
    // Check for data binding {path: "/..."}
    const TreeNode* pathNode = node.Find("path");
    if(pathNode && pathNode->GetType() == TreeNode::STRING)
    {
      std::string resolved = ctx.GetString(pathNode->GetString());
      // Try to determine if it's a boolean or number
      if(resolved == "true") return "true";
      if(resolved == "false") return "false";

      // Check if it's numeric
      char* endPtr = nullptr;
      std::strtod(resolved.c_str(), &endPtr);
      if(endPtr && *endPtr == '\0' && !resolved.empty())
      {
        return resolved; // numeric
      }

      {
        std::ostringstream oss;
        oss << "\"";
        EscapeJsonStr(oss, resolved);
        oss << "\"";
        return oss.str();
      }
    }

    // Nested object
    return BuildContextJson(node, ctx);
  }

  if(node.GetType() == TreeNode::INTEGER)
  {
    return std::to_string(node.GetInteger());
  }
  if(node.GetType() == TreeNode::FLOAT)
  {
    std::ostringstream oss;
    oss << node.GetFloat();
    return oss.str();
  }
  if(node.GetType() == TreeNode::BOOLEAN)
  {
    return node.GetBoolean() ? "true" : "false";
  }

  return "null";
}

} // namespace A2ui
