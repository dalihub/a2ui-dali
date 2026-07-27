#pragma once

/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd.
 * Licensed under the Apache License, Version 2.0.
 */

#include "core/surface-group-model.h"
#include "core/a2ui-message-processor.h"
#include "renderer/a2ui-renderer.h"

#include <dali-ui-foundation/dali-ui-foundation.h>
#include <functional>
#include <string>

namespace A2ui
{

/**
 * @brief Embeddable A2UI host facade — the the reference renderer A2UIRenderer analogue.
 *
 * Feed A2UI v0.9 JSONL with JsonFeed(); receive rendered DALi views and outgoing user
 * actions through callbacks. Routes by surfaceId, so multiple surfaces are supported
 * (createSurface for several ids → one BeginRenderingSurface event per surface).
 *
 *   A2uiHost host;
 *   host.GetRenderer().SetImageDir("res/");
 *   host.SetOnBeginRenderingSurface([&](auto id, auto view){ window.Add(view); });
 *   host.SetOnUserAction([&](auto id, auto json){ transport.Send(json); });
 *   host.JsonFeed(jsonl);
 *
 * The host owns the surface registry, message processor and renderer; the app no longer
 * wires those three together by hand. (Transport/streaming stay outside — the host is
 * transport-agnostic, exactly like the published library's design.)
 */
class A2uiHost
{
public:
  /// Fired when a surface's root view is (re)rendered. Host should add/replace by id.
  using RenderCallback = std::function<void(const std::string& surfaceId, Dali::Ui::View root)>;
  /// Fired when a surface is deleted. Host should remove its view.
  using DeleteCallback = std::function<void(const std::string& surfaceId)>;
  /// Fired when a component emits a user action (serialized A2UI `action` JSON).
  using ActionCallback = std::function<void(const std::string& surfaceId,
                                            const std::string& actionJson)>;

  A2uiHost() = default;

  /// Feed one or more A2UI v0.9 JSONL lines (newline separated).
  void JsonFeed(const std::string& jsonl);
  /// Feed a JSONL file. @return false if the file cannot be opened.
  bool JsonFeedFile(const std::string& path);

  void SetOnBeginRenderingSurface(RenderCallback cb) { mOnRender = std::move(cb); }
  void SetOnDeleteSurface(DeleteCallback cb) { mOnDelete = std::move(cb); }
  void SetOnUserAction(ActionCallback cb) { mOnAction = std::move(cb); }

  /// Access the renderer (e.g. for SetImageDir) and surface registry.
  A2uiRenderer& GetRenderer() { return mRenderer; }
  SurfaceGroupModel& GetSurfaces() { return mSurfaces; }

  /**
   * Drop every surface and start a fresh A2UI session.
   *
   * A surfaceId is unique per session, so replaying a stream the host has already shown
   * (a screen browser stepping back to an earlier example) needs the previous session
   * cleared first — otherwise its createSurface is correctly rejected as a duplicate.
   * Callbacks, image dir and other renderer settings are kept.
   */
  void Reset()
  {
    mSurfaces.Reset();
    mProcessor.Reset();
  }

private:
  void FeedLine(const std::string& line, bool deferRender = false);
  void RenderSurface(const std::string& surfaceId, SurfaceModel& surface);

  SurfaceGroupModel    mSurfaces;
  A2uiMessageProcessor mProcessor;
  A2uiRenderer         mRenderer;

  RenderCallback mOnRender;
  DeleteCallback mOnDelete;
  ActionCallback mOnAction;
};

} // namespace A2ui
