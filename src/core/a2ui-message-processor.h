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

#ifndef DALI_GENERATIVE_UI_A2UI_MESSAGE_PROCESSOR_H
#define DALI_GENERATIVE_UI_A2UI_MESSAGE_PROCESSOR_H

#include "surface-model.h"
#include "expression-parser.h"
#include "data-context.h"
#include <string>
#include <functional>
#include <set>

namespace A2ui
{

/**
 * Parses A2UI v0.9 JSONL messages and routes them to SurfaceModel.
 *
 * Supported messages:
 * - createSurface (with sendDataModel flag)
 * - updateComponents
 * - updateDataModel
 * - deleteSurface
 * - actionResponse (Phase 2)
 * - callFunction (Phase 4) — server calls client function
 */
class A2uiMessageProcessor
{
public:
  /**
   * Process a single JSONL line.
   * @param line  A single line of JSONL
   * @param surface  Target SurfaceModel to update
   * @return true if parsed and processed successfully
   */
  bool ProcessLine(const std::string& line, SurfaceModel& surface);

  /**
   * Process an entire JSONL file.
   * @param filePath  Path to the JSONL file
   * @param surface  Target SurfaceModel
   * @return true if all lines processed successfully
   */
  bool ProcessFile(const std::string& filePath, SurfaceModel& surface);

  /**
   * Process multi-line JSONL text (from editor).
   * @param text  Multi-line JSONL string
   * @param surface  Target SurfaceModel
   * @return true if all lines processed successfully
   */
  bool ProcessText(const std::string& text, SurfaceModel& surface);

  /**
   * Get the last error message.
   */
  const std::string& GetLastError() const { return mLastError; }

  /**
   * Start a fresh client session.
   *
   * Forgets which surfaceIds are active, so ids used before may be created again. A host
   * that tears down its surfaces (e.g. to load a different screen) must call this, or the
   * next createSurface for an id it already showed is reported as a duplicate.
   */
  void Reset()
  {
    mActiveSurfaceIds.clear();
    mLastError.clear();
  }

  /**
   * Set the expression parser for callFunction handling (Phase 4).
   */
  void SetExpressionParser(ExpressionParser* parser) { mExprParser = parser; }

  /**
   * Callback type for sending functionResponse back to server.
   */
  using FunctionResponseCallback = std::function<void(const std::string& functionCallId,
                                                       const std::string& value)>;

  void SetFunctionResponseCallback(FunctionResponseCallback cb)
  {
    mFunctionResponseCb = std::move(cb);
  }

private:
  bool OnCreateSurface(const Dali::Ui::Integration::TreeNode& msgBody, SurfaceModel& surface,
                       Dali::Ui::Integration::JsonParser parser);
  bool OnUpdateComponents(const Dali::Ui::Integration::TreeNode& msgBody, SurfaceModel& surface,
                          Dali::Ui::Integration::JsonParser parser);
  bool OnUpdateDataModel(const Dali::Ui::Integration::TreeNode& msgBody, SurfaceModel& surface);
  bool OnDeleteSurface(const Dali::Ui::Integration::TreeNode& msgBody, SurfaceModel& surface);
  /// @param envelope  The whole message — `functionCallId` and `wantResponse`
  ///                  are envelope-level siblings of `callFunction`.
  /// @param msgBody   The `callFunction` body, holding `call` and `args`.
  bool OnCallFunction(const Dali::Ui::Integration::TreeNode& envelope,
                      const Dali::Ui::Integration::TreeNode& msgBody,
                      SurfaceModel& surface);

  ExpressionParser* mExprParser = nullptr;
  FunctionResponseCallback mFunctionResponseCb;
  std::string mLastError;

  /// Surfaces this processor has seen a createSurface for and not yet a deleteSurface.
  /// A surfaceId must be unique per client session, so a second createSurface for one of
  /// these is a stream error rather than a silent re-create.
  std::set<std::string> mActiveSurfaceIds;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_A2UI_MESSAGE_PROCESSOR_H
