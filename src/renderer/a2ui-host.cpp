/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 */

#include "renderer/a2ui-host.h"
#include "core/data-model.h"

#include <dali/integration-api/debug.h>
#include <dali-ui-foundation/integration-api/builder/json-parser.h>
#include <fstream>
#include <sstream>
#include <vector>

using Dali::Ui::Integration::JsonParser;
using Dali::Ui::Integration::TreeNode;

namespace A2ui
{
namespace
{
struct LineInfo
{
  std::string surfaceId = "default";
  std::string kind;          // createSurface | updateComponents | updateDataModel | ...
  bool        isDelete  = false;
  bool        valid     = false;
};

// Peek the surfaceId + message kind from a JSONL line so the host can route it to the
// right surface in the group before handing it to the (single-surface) processor.
LineInfo PeekLine(const std::string& line)
{
  LineInfo info;
  JsonParser parser = JsonParser::New();
  if(!parser.Parse(line)) return info;
  const TreeNode* root = parser.GetRoot();
  if(!root) return info;

  static const char* kKeys[] = {
    "createSurface", "updateComponents", "updateDataModel", "deleteSurface", "callFunction"};
  for(const char* k : kKeys)
  {
    const TreeNode* body = root->Find(k);
    if(body)
    {
      const TreeNode* sid = body->Find("surfaceId");
      if(sid && sid->GetType() == TreeNode::STRING) info.surfaceId = sid->GetString();
      info.kind = k;
      info.isDelete = (info.kind == "deleteSurface");
      info.valid = true;
      return info;
    }
  }
  return info;
}

// Split a feed into individual A2UI message objects, accepting every shape a host might
// hand us: a JSON array "[{..},{..}]", newline-delimited JSONL "{..}\n{..}", several
// concatenated objects, or a single (possibly pretty-printed, multi-line) object. We
// scan brace depth — ignoring braces inside strings — and emit each top-level object,
// so newlines are irrelevant. This matches the reference renderer's JsonFeed(string), which takes
// a single object or a JSON array.
std::vector<std::string> SplitMessages(const std::string& s)
{
  std::size_t first = s.find_first_not_of(" \t\r\n");
  int base = (first != std::string::npos && s[first] == '[') ? 1 : 0; // objects sit one level in if array-wrapped
  std::vector<std::string> out;
  int depth = 0;
  bool inStr = false, esc = false;
  std::size_t start = std::string::npos;
  for(std::size_t i = 0; i < s.size(); ++i)
  {
    char c = s[i];
    if(inStr)
    {
      if(esc)            esc = false;
      else if(c == '\\') esc = true;
      else if(c == '"')  inStr = false;
      continue;
    }
    if(c == '"')                  { inStr = true; }
    else if(c == '{' || c == '[') { if(c == '{' && depth == base && start == std::string::npos) start = i; ++depth; }
    else if(c == '}' || c == ']')
    {
      --depth;
      if(c == '}' && depth == base && start != std::string::npos)
      {
        out.push_back(s.substr(start, i - start + 1));
        start = std::string::npos;
      }
    }
  }
  return out;
}
} // namespace

void A2uiHost::JsonFeed(const std::string& json)
{
  for(const std::string& msg : SplitMessages(json))
  {
    FeedLine(msg);
  }
}

bool A2uiHost::JsonFeedFile(const std::string& path)
{
  std::ifstream file(path);
  if(!file.is_open()) return false;
  std::stringstream buffer;
  buffer << file.rdbuf();

  // A file carries a whole surface (createSurface + updateComponents + updateDataModel)
  // at once. Process every message first, THEN render — so data-bound text and images
  // already hold their values at first paint instead of painting empty (which makes the
  // flex layout drop a trailing value's slot before its updateDataModel arrives). Live
  // streaming still uses JsonFeed()'s per-message render + Watch updates.
  for(const std::string& msg : SplitMessages(buffer.str()))
  {
    FeedLine(msg, /*deferRender=*/true);
  }
  for(const std::string& id : mSurfaces.GetSurfaceIds())
  {
    SurfaceModel* surface = mSurfaces.GetSurface(id);
    if(surface && surface->GetComponentsModel().GetRoot()) RenderSurface(id, *surface);
  }
  return true;
}

void A2uiHost::FeedLine(const std::string& rawLine, bool deferRender)
{
  // Preserve integers beyond int32 (e.g. a 850000000000 market cap) before the message is
  // parsed: the shared dali JSON parser would otherwise mangle them into a 32-bit field at
  // this first parse, before the data model ever sees them.
  std::string line = DataModel::WidenLargeIntegers(rawLine);
  LineInfo info = PeekLine(line);

  // Route to the surface for this id (creating it on demand → multi-surface).
  SurfaceModel& surface = mSurfaces.GetOrCreateSurface(info.surfaceId);

  if(!mProcessor.ProcessLine(line, surface))
  {
    DALI_LOG_ERROR("[A2UI][host] %s\n", mProcessor.GetLastError().c_str());
    return;
  }

  if(info.isDelete)
  {
    // A surface re-created under the same id must start from a full render.
    if(mRenderer.GetRenderedSurfaceId() == info.surfaceId) mRenderer.InvalidateRenderState();
    if(mOnDelete) mOnDelete(info.surfaceId);
    mSurfaces.DeleteSurface(info.surfaceId);
    return;
  }

  // Render on a structural change (updateComponents) once the surface has a root.
  // updateDataModel does not re-render — the data Watches update the existing views
  // in place — which also avoids rendering the surface twice on createSurface streams.
  // When deferRender is set (file/batch feed) the caller renders once at the end instead.
  if(!deferRender && info.kind == "updateComponents" && surface.GetComponentsModel().GetRoot())
  {
    RenderSurface(info.surfaceId, surface);
  }
}

void A2uiHost::RenderSurface(const std::string& surfaceId, SurfaceModel& surface)
{
  // Tag outgoing actions with this surface's id and route them to the host.
  ActionDispatcher& dispatcher = mRenderer.GetActionDispatcher();
  dispatcher.SetSurfaceId(surfaceId);
  dispatcher.SetSendDataModel(surface.GetSendDataModel());
  std::string sid = surfaceId;
  dispatcher.SetSendCallback([this, sid](const std::string& json) {
    if(mOnAction) mOnAction(sid, json);
  });

  // Use incremental rendering when a previous render exists; falls back to
  // full Render() inside the renderer if root changed or no prior render.
  Dali::Ui::View view = mRenderer.RenderIncremental(surface);
  if(view && mOnRender)
  {
    // RenderIncremental returns the same root view object for incremental
    // updates, so the callback fires with the current view — the caller's
    // mount logic handles "already mounted" gracefully.
    mOnRender(surfaceId, view);
  }
}

} // namespace A2ui
