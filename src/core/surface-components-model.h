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

#ifndef DALI_GENERATIVE_UI_SURFACE_COMPONENTS_MODEL_H
#define DALI_GENERATIVE_UI_SURFACE_COMPONENTS_MODEL_H

#include "component-model.h"
#include <unordered_map>
#include <dali-ui-foundation/integration-api/builder/tree-node.h>

namespace A2ui
{

/**
 * Manages the flat component list for a single surface.
 * Converts A2UI's adjacency-list components into an ID-based map.
 */
class SurfaceComponentsModel
{
public:
  /**
   * Parse the "components" JSON array and upsert into the ID map.
   * @param componentsArray TreeNode of type ARRAY containing component objects
   */
  void AddComponents(const Dali::Ui::Integration::TreeNode& componentsArray);

  /**
   * Get a component by ID.
   * @return pointer to ComponentModel, or nullptr if not found
   */
  const ComponentModel* GetComponent(const std::string& id) const;

  /**
   * Get the root component (id == "root").
   */
  const ComponentModel* GetRoot() const;

  /**
   * Get total component count.
   */
  size_t GetCount() const { return mComponents.size(); }

  /**
   * Clear all components.
   */
  void Clear();

  /**
   * Get the full component map (for diff engine).
   */
  const std::unordered_map<std::string, ComponentModel>& GetAll() const { return mComponents; }

  /**
   * Get the set of IDs that were modified in the last AddComponents call.
   */
  const std::vector<std::string>& GetLastModifiedIds() const { return mLastModifiedIds; }

  /// Parent reverse index: find the parent of a child component.
  /// @return parent component ID, or empty string if not found.
  std::string GetParentId(const std::string& childId) const;

  /// Get the index of a child within its parent's childIds list.
  /// @return 0-based index, or -1 if the child is not a list child (or not found).
  int GetChildIndexInParent(const std::string& childId) const;

private:
  std::unordered_map<std::string, ComponentModel> mComponents;
  std::vector<std::string> mLastModifiedIds;

  // childId → parentId reverse index, maintained during AddComponents.
  std::unordered_map<std::string, std::string> mParentMap;
};


} // namespace A2ui

#endif // DALI_GENERATIVE_UI_SURFACE_COMPONENTS_MODEL_H
