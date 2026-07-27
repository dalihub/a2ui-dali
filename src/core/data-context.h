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

#ifndef DALI_GENERATIVE_UI_DATA_CONTEXT_H
#define DALI_GENERATIVE_UI_DATA_CONTEXT_H

#include "data-model.h"
#include <string>

namespace A2ui
{

/**
 * Scoped data context for resolving relative and absolute JSON Pointer paths.
 *
 * In template children, each array item gets a child context with its own scope
 * (e.g. "/employees/0"), so relative paths like "name" resolve to "/employees/0/name".
 */
class DataContext
{
public:
  DataContext(DataModel& dm, const std::string& scope = "/");

  /**
   * Resolve a path (absolute or relative) to an absolute JSON Pointer.
   * - Paths starting with "/" are absolute and returned as-is.
   * - Other paths are relative to the current scope.
   */
  std::string Resolve(const std::string& path) const;

  /**
   * Create a child context with a narrower scope.
   * @param childScope  Absolute path for the child scope (e.g. "/employees/0")
   */
  DataContext CreateChildContext(const std::string& childScope) const;

  /**
   * Create a child context by appending an index to the current scope.
   * @param index  Array index to append
   */
  DataContext CreateChildContextForIndex(int index) const;

  /**
   * Create the context for one item of a list template.
   *
   * Same as CreateChildContext, but also records the iteration index so the `@index`
   * built-in can read it. A2UI restricts `@index` to template instantiation (Collection
   * Scope), which is exactly the set of contexts made through this call.
   *
   * @param childScope  Absolute path for the item (e.g. "/employees/2")
   * @param index       0-based iteration index
   */
  DataContext CreateCollectionItemContext(const std::string& childScope, int index) const;

  /// 0-based iteration index when this context is a list-template item, else -1.
  int GetCollectionIndex() const { return mCollectionIndex; }

  // Data access (delegates to DataModel with resolved paths)
  std::string GetString(const std::string& path) const;
  float       GetFloat(const std::string& path, float fallback = 0.0f) const;
  bool        GetBool(const std::string& path, bool fallback = false) const;
  const Dali::Ui::Integration::TreeNode* ResolvePath(const std::string& path) const;

  // Data mutation (delegates to DataModel with resolved paths)
  bool SetValue(const std::string& path, const std::string& value);
  bool SetBoolValue(const std::string& path, bool value);

  // Observer
  uint32_t Watch(const std::string& path, DataChangeCallback cb);

  DataModel&         GetDataModel() { return mDataModel; }
  const DataModel&   GetDataModel() const { return mDataModel; }
  const std::string& GetScope() const { return mScope; }

private:
  DataModel&  mDataModel;
  std::string mScope;
  int         mCollectionIndex = -1;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_DATA_CONTEXT_H
