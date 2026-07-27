/* Copyright (c) 2026 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

// a2ui-dali gallery demo — a runnable browser for the standard A2UI catalog.
//
//   ./bin/a2ui-gallery-demo [screens-dir]      (default: examples/gallery-demo/screens)
//
// Each file in the directory is one A2UI example stream (a JSON array of messages). The
// demo feeds them through the A2uiHost facade one at a time and shows the rendered
// surface; press Space / → for the next example, ← for the previous, Esc to quit. This
// mirrors how a host app drives the renderer: create host → subscribe callbacks → JsonFeed.

#include "renderer/a2ui-host.h"

#include <dali/public-api/adaptor-framework/application.h>
#include <dali/public-api/adaptor-framework/window-data.h>
#include <dali/public-api/adaptor-framework/window.h>
#include <dali/public-api/events/key-event.h>
#include <dali/public-api/math/rect.h>
#include <dali-ui-foundation/dali-ui-foundation.h>

#include <dirent.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
using namespace Dali;
using namespace Dali::Ui;

class GalleryDemo : public ConnectionTracker
{
public:
  GalleryDemo(Application& app, std::string dir)
  : mApp(app), mDir(std::move(dir))
  {
    mApp.InitSignal().Connect(this, &GalleryDemo::OnInit);
  }

private:
  void OnInit(Application app)
  {
    Window window = app.GetWindow();
    window.SetBackgroundColor(UiColor(0xFFFFFF));
    window.KeyEventSignal().Connect(this, &GalleryDemo::OnKey);

    mRoot = FlexLayout::New();
    mRoot.SetDirection(FlexDirection::COLUMN);
    mRoot.SetRequestedWidth(MATCH_PARENT);
    mRoot.SetRequestedHeight(MATCH_PARENT);
    window.Add(mRoot);

    mTitle = Label::New("");
    mTitle.SetFontSize(13);
    mTitle.SetTextColor(UiColor(0x636368));
    mTitle.SetMargin(Extents(16, 16, 12, 10));
    mRoot.Add(mTitle);

    mContent = FlexLayout::New();
    mContent.SetDirection(FlexDirection::COLUMN);
    mContent.SetRequestedWidth(MATCH_PARENT);
    mContent.SetPadding(Extents(16, 16, 0, 0));
    mRoot.Add(mContent);

    // The whole app integration: point the host at resources, subscribe, feed.
    mHost.GetRenderer().SetImageDir("res/");
    mHost.SetOnBeginRenderingSurface([this](const std::string&, View root) {
      while(mContent.GetChildCount() > 0) mContent.Remove(mContent.GetChildAt(0u));
      mContent.Add(root);
      // Give the TV remote a starting point: focus the surface's first focusable view.
      // (Screen paging still works via Space; arrow keys now also move focus between views.)
      FocusManager::Get().RequestFocus(root);
    });
    mHost.SetOnUserAction([](const std::string& surfaceId, const std::string& json) {
      std::printf("[a2ui-dali] action surface=%s %s\n", surfaceId.c_str(), json.c_str());
    });

    ListScreens();
    if(mScreens.empty())
    {
      mTitle.SetText(Dali::String(("No example files in '" + mDir + "'").c_str()));
      return;
    }
    Show(0);
  }

  void ListScreens()
  {
    DIR* dir = opendir(mDir.c_str());
    if(!dir) return;
    for(dirent* ent; (ent = readdir(dir)) != nullptr;)
    {
      std::string name = ent->d_name;
      auto ext = name.find_last_of('.');
      if(ext != std::string::npos)
      {
        std::string e = name.substr(ext);
        if(e == ".json" || e == ".jsonl") mScreens.push_back(mDir + "/" + name);
      }
    }
    closedir(dir);
    std::sort(mScreens.begin(), mScreens.end());
  }

  void Show(int index)
  {
    if(mScreens.empty()) return;
    int n = static_cast<int>(mScreens.size());
    mIndex = ((index % n) + n) % n;

    while(mContent.GetChildCount() > 0) mContent.Remove(mContent.GetChildAt(0u));
    // Each screen is its own A2UI session. Without this, stepping back to a screen already
    // visited would replay its createSurface for a surfaceId still active — which the
    // processor correctly rejects as a duplicate.
    mHost.Reset();
    mHost.JsonFeedFile(mScreens[mIndex]);

    std::string base = mScreens[mIndex];
    auto slash = base.find_last_of('/');
    if(slash != std::string::npos) base = base.substr(slash + 1);
    std::string label = std::to_string(mIndex + 1) + " / " + std::to_string(n) + "   " + base +
                        "      [Space / → next   ← prev   Esc quit]";
    mTitle.SetText(Dali::String(label.c_str()));
  }

  void OnKey(Window /*window*/, KeyEvent event)
  {
    if(event.GetState() != KeyEvent::DOWN) return;
    const Dali::String key = event.GetKeyName();
    if(key == "Right" || key == "space" || key == "Next")      Show(mIndex + 1);
    else if(key == "Left" || key == "Prior" || key == "BackSpace") Show(mIndex - 1);
    else if(key == "Escape")                                    mApp.Quit();
  }

  Application&  mApp;
  std::string   mDir;
  std::vector<std::string> mScreens;
  int           mIndex = 0;

  A2ui::A2uiHost mHost;
  FlexLayout     mRoot;
  FlexLayout     mContent;
  Label          mTitle;
};

} // namespace

int DALI_EXPORT_API main(int argc, char** argv)
{
  std::string dir = (argc > 1) ? argv[1] : "examples/gallery-demo/screens";

  Dali::WindowData windowData;
  Dali::Rect<int> positionSize(0, 0, 720, 1280);
  windowData.SetPositionSize(positionSize);
  Dali::Application application = Dali::Application::New(&argc, &argv, "", false, windowData);
  Dali::Ui::UiConfig::New().Apply();

  GalleryDemo demo(application, dir);
  application.MainLoop();
  return 0;
}
