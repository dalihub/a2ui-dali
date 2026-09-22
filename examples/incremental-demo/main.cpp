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

// Incremental rendering demo.
//
//   ./a2ui-incremental-demo [path/to/dir] [delay_ms] [count]
//
// Reads all *.json files from a directory (default: ../res/a2ui), sorts them
// by filename, and feeds each one to A2uiHost with a delay between messages.
// If count > 0, only the first `count` messages are executed (0 = all).
// Prints detailed logs showing whether each message triggered a FULL render
// or an INCREMENTAL render, proving that non-root updateComponents use the
// incremental path.


#include "renderer/a2ui-host.h"

#include <dali/public-api/adaptor-framework/application.h>
#include <dali/public-api/adaptor-framework/timer.h>
#include <dali/public-api/adaptor-framework/window-data.h>
#include <dali/public-api/math/rect.h>
#include <dali-ui-foundation/dali-ui-foundation.h>

#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace
{

/// Read an entire file into a string.
bool ReadFile(const std::string& path, std::string& out)
{
  std::ifstream f(path);
  if(!f.is_open()) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

/// List all .json files in a directory, sorted by name.
std::vector<std::string> ListJsonFiles(const std::string& dir)
{
  std::vector<std::string> result;
  DIR* d = opendir(dir.c_str());
  if(!d) return result;
  struct dirent* ent;
  while((ent = readdir(d)) != nullptr)
  {
    std::string name = ent->d_name;
    // Accept .json files only
    if(name.size() > 5 && name.substr(name.size() - 5) == ".json")
    {
      result.push_back(name);
    }
  }
  closedir(d);
  std::sort(result.begin(), result.end());
  return result;
}

/// Extract the message type from a JSON string (createSurface / updateComponents / updateDataModel).
std::string GetMessageType(const std::string& json)
{
  if(json.find("\"createSurface\"") != std::string::npos)    return "createSurface";
  if(json.find("\"updateComponents\"") != std::string::npos) return "updateComponents";
  if(json.find("\"updateDataModel\"") != std::string::npos)  return "updateDataModel";
  return "unknown";
}

/// Check if "root" appears as a component id in the JSON (triggers full re-render).
bool ContainsRootComponent(const std::string& json)
{
  // Look for "id":"root" pattern (the root component being re-declared)
  return json.find("\"id\":\"root\"") != std::string::npos ||
         json.find("\"id\": \"root\"") != std::string::npos;
}


struct RenderStats
{
  int totalMessages = 0;
  int fullRenders = 0;       // createSurface + root changes
  int incrementalRenders = 0; // non-root updateComponents
  int dataModelUpdates = 0;  // updateDataModel (no render)
  int rootPreserved = 0;     // times root view object was kept
};

class IncrementalDemoApp : public Dali::ConnectionTracker
{
public:
  IncrementalDemoApp(Dali::Application& app, std::string dir, int delayMs, int maxCount)
  : mApplication(app)
  , mDir(std::move(dir))
  , mDelayMs(delayMs)
  , mMaxCount(maxCount)
  {
    mApplication.InitSignal().Connect(this, &IncrementalDemoApp::OnInit);
  }


private:
  void OnInit(Dali::Application app)
  {
    Dali::Window window = app.GetWindow();
    window.SetBackgroundColor(Dali::Ui::UiColor(0x333333));


    // Root container
    Dali::Ui::FlexLayout rootLayout = Dali::Ui::FlexLayout::New();
    rootLayout.SetDirection(Dali::Ui::FlexDirection::COLUMN);
    rootLayout.SetRequestedWidth(Dali::Ui::MATCH_PARENT);
    rootLayout.SetRequestedHeight(Dali::Ui::MATCH_PARENT);
    rootLayout.SetPadding(Dali::Extents(16, 16, 16, 16));
    window.Add(rootLayout);
    mRootLayout = rootLayout;

    // Host setup
    mHost.GetRenderer().SetImageDir("res/");
    mHost.SetOnBeginRenderingSurface([this](const std::string&, Dali::Ui::View view) {
      // Replace previous surface view
      if(mSurfaceView)
      {
        mRootLayout.Remove(mSurfaceView);
      }
      mSurfaceView = view;
      if(mSurfaceView)
      {
        mRootLayout.Add(mSurfaceView);
        Dali::Ui::FocusManager::Get().RequestFocus(mSurfaceView);
      }
    });
    mHost.SetOnUserAction([](const std::string& surfaceId, const std::string& json) {
      std::printf("[action] surface=%s %s\n", surfaceId.c_str(), json.c_str());
    });

    // Load file list
    mFiles = ListJsonFiles(mDir);
    if(mFiles.empty())
    {
      std::fprintf(stderr, "No .json files found in '%s'\n", mDir.c_str());
      mApplication.Quit();
      return;
    }

    std::printf("========================================\n");
    std::printf("  a2ui-dali Incremental Render Demo\n");
    std::printf("  Directory: %s\n", mDir.c_str());
    std::printf("  Messages:  %zu\n", mFiles.size());
    std::printf("  Delay:     %d ms between messages\n", mDelayMs);
    std::printf("========================================\n\n");

    // Start feeding messages via a timer
    mCurrentIndex = 0;
    mTimer = Dali::Timer::New(mDelayMs);
    mTimer.TickSignal().Connect(this, &IncrementalDemoApp::OnTick);
    mTimer.Start();
  }

  bool OnTick()
  {
    // Determine how many messages to process: mMaxCount (0 = all)
    int limit = static_cast<int>(mFiles.size());
    if(mMaxCount > 0 && mMaxCount < limit)
    {
      limit = mMaxCount;
    }

    if(mCurrentIndex >= limit)
    {
      // All done — print summary. Don't quit: keep the final rendered UI
      // on screen so the user can see the result.
      PrintSummary();
      mTimer.Stop();
      return false;
    }




    const std::string& filename = mFiles[mCurrentIndex];
    std::string        fullPath = mDir + "/" + filename;
    std::string        content;
    if(!ReadFile(fullPath, content))
    {
      std::fprintf(stderr, "[step %d/%zu] FAILED to read %s\n",
                   mCurrentIndex + 1, mFiles.size(), filename.c_str());
      mCurrentIndex++;
      return true;
    }

    std::string msgType    = GetMessageType(content);
    bool        wasRendered = mHost.GetRenderer().IsRendered();
    bool        hasRoot     = ContainsRootComponent(content);
    bool        willBeFull  = false;

    // Predict render mode
    if(msgType == "createSurface")
    {
      willBeFull = true;
    }
    else if(msgType == "updateComponents")
    {
      willBeFull = (!wasRendered || hasRoot);
    }
    // updateDataModel doesn't trigger render

    // Capture root view before feed (to check preservation)
    Dali::Ui::View prevRoot = mHost.GetRenderer().GetDiffEngine().GetView("root");

    std::printf("[step %d/%zu] %s → %s\n",
                mCurrentIndex + 1, mFiles.size(), filename.c_str(), msgType.c_str());

    if(msgType == "updateComponents")
    {
      if(!wasRendered)
      {
        std::printf("  → No prior render → FULL Render()\n");
      }
      else if(hasRoot)
      {
        std::printf("  → root in changed set → FULL Render()\n");
      }
      else
      {
        std::printf("  → root NOT in changed set → INCREMENTAL Render()\n");
      }
    }
    else if(msgType == "createSurface")
    {
      std::printf("  → createSurface → FULL Render() (initial)\n");
    }
    else if(msgType == "updateDataModel")
    {
      std::printf("  → updateDataModel (data only, no component render)\n");
    }

    // Feed the message
    mHost.JsonFeed(content);

    // Check post-feed state
    bool        nowRendered = mHost.GetRenderer().IsRendered();
    Dali::Ui::View newRoot  = mHost.GetRenderer().GetDiffEngine().GetView("root");

    if(msgType == "updateComponents" || msgType == "createSurface")
    {
      if(willBeFull)
      {
        mStats.fullRenders++;
        std::printf("  ← FULL render done, IsRendered()=%d\n", nowRendered ? 1 : 0);
      }
      else
      {
        mStats.incrementalRenders++;
        bool preserved = (prevRoot == newRoot);
        if(preserved)
        {
          mStats.rootPreserved++;
          std::printf("  ← INCREMENTAL render done, root view PRESERVED (same object)\n");
        }
        else
        {
          std::printf("  ← INCREMENTAL render done, root view CHANGED (unexpected)\n");
        }
      }
    }
    else if(msgType == "updateDataModel")
    {
      mStats.dataModelUpdates++;
      std::printf("  ← Data model updated (data-bound views refresh via watchers)\n");
    }
    std::printf("\n");

    mStats.totalMessages++;
    mCurrentIndex++;
    return true; // continue timer
  }

  void PrintSummary()
  {

    std::printf("========================================\n");
    std::printf("  Incremental Render Summary\n");
    std::printf("========================================\n");
    std::printf("  Total messages:    %d\n", mStats.totalMessages);
    std::printf("  Full renders:      %d  (createSurface + root changes)\n", mStats.fullRenders);
    std::printf("  Incremental:       %d  (non-root component updates)\n", mStats.incrementalRenders);
    std::printf("  Data model updates:%d  (no component render)\n", mStats.dataModelUpdates);
    std::printf("  Root preserved:    %d  times (incremental kept same root view)\n", mStats.rootPreserved);
    std::printf("========================================\n");
  }

  Dali::Application&     mApplication;
  std::string           mDir;
  int                   mDelayMs;
  A2ui::A2uiHost        mHost;
  Dali::Ui::FlexLayout  mRootLayout;
  Dali::Ui::View        mSurfaceView;
  Dali::Timer           mTimer;
  Dali::Timer           mHoldTimer;
  std::vector<std::string> mFiles;

  int                   mCurrentIndex = 0;
  int                   mMaxCount = 0;  // 0 = all messages
  RenderStats           mStats;

};


} // anonymous namespace

int DALI_EXPORT_API main(int argc, char** argv)
{
  // Usage: ./a2ui-incremental-demo [dir] [delay_ms] [count]
  //   dir       — directory with .json files (default: ../res/a2ui)
  //   delay_ms  — delay between messages in ms (default: 800)
  //   count     — number of messages to execute, 0 = all (default: 0)
  std::string dir = (argc > 1) ? argv[1] : "../res/a2ui";
  int delayMs = (argc > 2) ? std::atoi(argv[2]) : 800;
  int maxCount = (argc > 3) ? std::atoi(argv[3]) : 0;

  std::printf("Usage: %s [dir] [delay_ms] [count]\n", argv[0]);
  std::printf("  dir=%s  delay=%dms  count=%s\n",
              dir.c_str(), delayMs,
              maxCount > 0 ? std::to_string(maxCount).c_str() : "all");

  Dali::WindowData windowData;
  Dali::Rect<int>  positionSize(0, 0, 720, 1080);
  windowData.SetPositionSize(positionSize);
  Dali::Application application = Dali::Application::New(&argc, &argv, "", false, windowData);

  Dali::Ui::UiConfig uiConfig = Dali::Ui::UiConfig::New();
  uiConfig.SetDpi(160);
  uiConfig.Apply();

  IncrementalDemoApp demo(application, dir, delayMs, maxCount);

  application.MainLoop();
  return 0;
}
