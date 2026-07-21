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

#ifndef DALI_GENERATIVE_UI_DATA_MODEL_H
#define DALI_GENERATIVE_UI_DATA_MODEL_H

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <dali-ui-foundation/integration-api/builder/json-parser.h>
#include <dali-ui-foundation/integration-api/builder/tree-node.h>

namespace A2ui
{

/**
 * Callback type for data change notifications.
 * @param path     The absolute path that changed
 * @param newValue The new value as string
 */
using DataChangeCallback = std::function<void(const std::string& path, const std::string& newValue)>;

/**
 * Stores JSON data, resolves JSON Pointer (RFC 6901) paths,
 * and provides observer-based change notifications for two-way data binding.
 */
class DataModel
{
public:
  /**
   * Quote integer literals that overflow int32 (e.g. a 850000000000 market cap) so the
   * shared dali JSON parser keeps them verbatim instead of mangling them into a 32-bit
   * field. Apply to a raw message *before* it is first parsed; floats and in-range integers
   * are untouched, and string contents are never rewritten. Idempotent.
   */
  static std::string WidenLargeIntegers(const std::string& json);

  /**
   * Format a float the way the web (JS) prints a number: the shortest decimal that
   * round-trips to within one ULP of the value (undoes the dali parser's last-bit rounding,
   * e.g. 0.45000005 → "0.45"; 12458.32 stays exact). Shared so every float→string path
   * (GetString, expression args, inline text) formats identically.
   */
  static std::string FormatFloatToken(float value);

  // --- Data mutation (server → client) ---

  /**
   * Set data at the given JSON Pointer path.
   * Notifies observers on the affected path.
   */
  bool SetData(const std::string& path, const std::string& jsonString);

  /**
   * Set data from a pre-parsed TreeNode value at the given path.
   * Notifies observers on the affected path.
   */
  bool SetDataFromNode(const std::string& path, const Dali::Ui::Integration::TreeNode& value);

  /**
   * Set a single string value at an arbitrary JSON Pointer path.
   * Supports nested paths (e.g. "/contact/firstName").
   * This is the primary method for input components to write back to the model.
   */
  bool SetValue(const std::string& path, const std::string& value);

  /**
   * Set a boolean value at an arbitrary JSON Pointer path.
   */
  bool SetBoolValue(const std::string& path, bool value);

  /**
   * Set a numeric value at an arbitrary JSON Pointer path.
   */
  bool SetFloatValue(const std::string& path, float value);

  // --- Data reading ---

  const Dali::Ui::Integration::TreeNode* ResolvePath(const std::string& path) const;
  std::string GetString(const std::string& path) const;
  float GetFloat(const std::string& path, float fallback = 0.0f) const;
  bool GetBool(const std::string& path, bool fallback = false) const;
  bool HasData() const { return !!mParser; }
  std::string Serialize() const;

  // --- Observer pattern ---

  /**
   * Register a callback for changes at the given path.
   * @return observer ID (for Unwatch)
   */
  uint32_t Watch(const std::string& path, DataChangeCallback cb);

  /**
   * Unregister a previously registered observer.
   */
  void Unwatch(uint32_t observerId);

  /**
   * Unregister a batch of observers at once.
   *
   * Rebuilding a subtree destroys its views; without retiring their watches the observers
   * would live on, repainting detached actors and growing the list on every rebuild.
   * Safe to call from inside a notification: registrations still queued for this pass are
   * dropped too, which a deferred removal alone would miss (pending removals are applied
   * before pending additions).
   */
  void UnwatchAll(const std::vector<uint32_t>& observerIds);

  /**
   * How many observers are currently registered (diagnostics — a rebuild that fails to
   * retire its old watches shows up here as a count that keeps climbing).
   */
  size_t ObserverCount() const { return mObservers.size(); }

  /**
   * Clear all data and observers.
   */
  void Clear();

  /**
   * Clear all observers only (keeps data intact).
   */
  void ClearObservers();

private:
  bool MergeAtPath(const std::string& path, const std::string& valueJson);
  void NotifyObservers(const std::string& path);

  static std::string TreeNodeToJson(const Dali::Ui::Integration::TreeNode& node);
  static void EscapeJsonString(std::ostringstream& oss, const char* str);

  Dali::Ui::Integration::JsonParser mParser;
  std::string          mJsonString;

  struct Observer
  {
    uint32_t           id;
    std::string        path;
    DataChangeCallback callback;
  };
  std::vector<Observer>   mObservers;
  std::vector<Observer>   mPendingAdds;
  std::vector<uint32_t>   mPendingRemoves;
  uint32_t                mNextObserverId = 1;
  bool                    mNotifying = false; // re-entrancy guard
  bool                    mClearObserversPending = false; // ClearObservers() during a notify
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_DATA_MODEL_H
