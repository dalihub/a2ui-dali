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

#include "a2ui-message-processor.h"
#include "a2ui-sanitizer.h"
#include <dali/integration-api/debug.h>
#include <dali-ui-foundation/integration-api/builder/json-parser.h>
#include <fstream>
#include <sstream>

using Dali::Ui::Integration::JsonParser;
using Dali::Ui::Integration::TreeNode;

namespace A2ui
{

bool A2uiMessageProcessor::ProcessLine(const std::string& line, SurfaceModel& surface)
{
  if(line.empty())
  {
    return true; // Skip empty lines
  }

  // Phase 4: Validate data size
  if(!A2uiSanitizer::ValidateDataSize(line.size()))
  {
    mLastError = "Message exceeds maximum data size";
    return false;
  }

  // Parse JSON
  JsonParser parser = JsonParser::New();
  if(!parser.Parse(line))
  {
    mLastError = "JSON parse error at column " +
                 std::to_string(parser.GetErrorPosition()) + ": " +
                 parser.GetErrorDescription();
    DALI_LOG_ERROR("[A2UI] %s\n", mLastError.c_str());
    return false;
  }

  const TreeNode* root = parser.GetRoot();
  if(!root)
  {
    mLastError = "Empty JSON object";
    return false;
  }

  // Check version
  const TreeNode* versionNode = root->Find("version");
  if(versionNode && versionNode->GetType() == TreeNode::STRING)
  {
    // Accept v0.9 (primary target) and other versions gracefully
    // In Phase 3, A2uiValidator will enforce strict version checking
  }

  // Route by message type
  const TreeNode* createSurface = root->Find("createSurface");
  if(createSurface)
  {
    return OnCreateSurface(*createSurface, surface, parser);
  }

  const TreeNode* updateComponents = root->Find("updateComponents");
  if(updateComponents)
  {
    return OnUpdateComponents(*updateComponents, surface, parser);
  }

  const TreeNode* updateDataModel = root->Find("updateDataModel");
  if(updateDataModel)
  {
    return OnUpdateDataModel(*updateDataModel, surface);
  }

  const TreeNode* deleteSurface = root->Find("deleteSurface");
  if(deleteSurface)
  {
    return OnDeleteSurface(*deleteSurface, surface);
  }

  // Phase 4: callFunction (server → client)
  const TreeNode* callFunction = root->Find("callFunction");
  if(callFunction)
  {
    return OnCallFunction(*root, *callFunction, surface);
  }

  mLastError = "Unknown message type";
  DALI_LOG_ERROR("[A2UI] Unknown message type in line\n");
  return false;
}

bool A2uiMessageProcessor::ProcessFile(const std::string& filePath, SurfaceModel& surface)
{
  std::ifstream file(filePath);
  if(!file.is_open())
  {
    mLastError = "Failed to open file: " + filePath;
    DALI_LOG_ERROR("[A2UI] %s\n", mLastError.c_str());
    return false;
  }

  std::string line;
  int lineNum = 0;
  while(std::getline(file, line))
  {
    lineNum++;
    if(line.empty()) continue;

    if(!ProcessLine(line, surface))
    {
      mLastError = "Line " + std::to_string(lineNum) + ": " + mLastError;
      return false;
    }
  }

  return true;
}

bool A2uiMessageProcessor::ProcessText(const std::string& text, SurfaceModel& surface)
{
  std::istringstream stream(text);
  std::string line;
  int lineNum = 0;

  while(std::getline(stream, line))
  {
    lineNum++;
    if(line.empty()) continue;

    if(!ProcessLine(line, surface))
    {
      mLastError = "Line " + std::to_string(lineNum) + ": " + mLastError;
      return false;
    }
  }

  return true;
}

bool A2uiMessageProcessor::OnCreateSurface(const TreeNode& msgBody, SurfaceModel& surface,
                                            JsonParser parser)
{
  const TreeNode* surfaceIdNode = msgBody.Find("surfaceId");
  const TreeNode* catalogIdNode = msgBody.Find("catalogId");

  std::string surfaceId = (surfaceIdNode && surfaceIdNode->GetType() == TreeNode::STRING)
                          ? surfaceIdNode->GetString() : "default";
  std::string catalogId = (catalogIdNode && catalogIdNode->GetType() == TreeNode::STRING)
                          ? catalogIdNode->GetString() : "basic";

  surface.Create(surfaceId, catalogId);

  // Phase 2: sendDataModel flag
  const TreeNode* sendDmNode = msgBody.Find("sendDataModel");
  if(sendDmNode && sendDmNode->GetType() == TreeNode::BOOLEAN)
  {
    surface.SetSendDataModel(sendDmNode->GetBoolean());
  }

  // theme { width, height, pattern } + sourceApp (the reference renderer SurfaceContext parity).
  const TreeNode* themeNode = msgBody.Find("theme");
  if(themeNode && themeNode->GetType() == TreeNode::OBJECT)
  {
    auto readFloat = [](const TreeNode* o, const char* key) -> float {
      const TreeNode* n = o->Find(key);
      if(n && n->GetType() == TreeNode::FLOAT) return n->GetFloat();
      if(n && n->GetType() == TreeNode::INTEGER) return static_cast<float>(n->GetInteger());
      return 0.0f;
    };
    const TreeNode* patNode = themeNode->Find("pattern");
    std::string pattern = (patNode && patNode->GetType() == TreeNode::STRING)
                          ? patNode->GetString() : "";
    surface.SetTheme(readFloat(themeNode, "width"), readFloat(themeNode, "height"), pattern);
  }
  const TreeNode* srcAppNode = msgBody.Find("sourceApp");
  if(srcAppNode && srcAppNode->GetType() == TreeNode::STRING)
  {
    surface.SetSourceApp(srcAppNode->GetString());
  }

  surface.KeepParser(parser);
  return true;
}

bool A2uiMessageProcessor::OnUpdateComponents(const TreeNode& msgBody, SurfaceModel& surface,
                                               JsonParser parser)
{
  if(!surface.IsCreated())
  {
    mLastError = "updateComponents before createSurface";
    DALI_LOG_ERROR("[A2UI] %s\n", mLastError.c_str());
    return false;
  }

  const TreeNode* componentsNode = msgBody.Find("components");
  if(!componentsNode)
  {
    mLastError = "updateComponents missing 'components' array";
    return false;
  }

  // Keep parser alive so TreeNode pointers in ComponentModel remain valid
  surface.KeepParser(parser);
  surface.UpdateComponents(*componentsNode);
  return true;
}

bool A2uiMessageProcessor::OnUpdateDataModel(const TreeNode& msgBody, SurfaceModel& surface)
{
  if(!surface.IsCreated())
  {
    mLastError = "updateDataModel before createSurface";
    DALI_LOG_ERROR("[A2UI] %s\n", mLastError.c_str());
    return false;
  }

  const TreeNode* pathNode  = msgBody.Find("path");
  const TreeNode* valueNode = msgBody.Find("value");

  std::string path = "/";
  if(pathNode && pathNode->GetType() == TreeNode::STRING)
  {
    path = pathNode->GetString();
  }

  if(!valueNode)
  {
    mLastError = "updateDataModel missing 'value'";
    return false;
  }

  surface.UpdateDataModel(path, *valueNode);
  return true;
}

bool A2uiMessageProcessor::OnDeleteSurface(const TreeNode& msgBody, SurfaceModel& surface)
{
  surface.Delete();
  return true;
}

bool A2uiMessageProcessor::OnCallFunction(const TreeNode& envelope, const TreeNode& msgBody,
                                          SurfaceModel& surface)
{
  // Wire shape:
  //   {"version":"v0.9", "functionCallId":"fc_001", "wantResponse":true,
  //    "callFunction":{"call":"formatDate", "args":{...}}}
  // functionCallId and wantResponse sit on the envelope; only call/args are in
  // the body, where ExpressionParser can consume them directly.
  const TreeNode* callIdNode = envelope.Find("functionCallId");
  std::string functionCallId;
  if(callIdNode && callIdNode->GetType() == TreeNode::STRING)
  {
    functionCallId = callIdNode->GetString();
  }

  const TreeNode* callNode = msgBody.Find("call");
  if(!callNode)
  {
    mLastError = "callFunction message missing 'call' field for function spec";
    DALI_LOG_ERROR("[A2UI] %s\n", mLastError.c_str());
    return false;
  }

  const TreeNode* wantResponseNode = envelope.Find("wantResponse");
  bool wantResponse = wantResponseNode && wantResponseNode->GetType() == TreeNode::BOOLEAN
                      && wantResponseNode->GetBoolean();

  // Execute function via ExpressionParser
  std::string result;
  if(mExprParser)
  {
    DataContext ctx(surface.GetDataModel());
    // msgBody itself contains "call" and "args" — pass it directly to Evaluate
    result = mExprParser->Evaluate(msgBody, ctx);
  }
  else
  {
    DALI_LOG_ERROR("[A2UI] callFunction: no ExpressionParser set\n");
    result = "";
  }

  // Send response if requested
  if(wantResponse && mFunctionResponseCb)
  {
    mFunctionResponseCb(functionCallId, result);
  }

  return true;
}

} // namespace A2ui
