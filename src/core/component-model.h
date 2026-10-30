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

#ifndef DALI_GENERATIVE_UI_COMPONENT_MODEL_H
#define DALI_GENERATIVE_UI_COMPONENT_MODEL_H

#include <string>
#include <vector>
#include <dali-ui-foundation/integration-api/builder/tree-node.h>

// TreeNode and JsonParser are in Dali::Ui namespace (dali-ui-foundation),
// not Dali::Toolkit (dali-toolkit).

namespace A2ui
{

/**
 * Single A2UI component state.
 * Stores the component ID, type, children IDs, and a pointer to the raw JSON node
 * for property access.
 */
struct ComponentModel
{
  std::string              id;
  std::string              type;      // "Text", "Row", "Column", "Card", "Button", "Image", "Divider"
  const Dali::Ui::Integration::TreeNode* rawNode = nullptr; // Original JSON node for property lookup
  std::vector<std::string> childIds;  // children: ["id1", "id2"] (Row, Column)
  std::string              childId;   // child: "single_id" (Card, Button)

  /// Whether this component needs its parent to be re-rendered when it changes.
  /// Container types (Button, Card, Row, Column, Tabs, List, Modal) = true:
  /// their parent's render logic depends on child content/type/properties.
  /// Leaf types (Text, Image, Icon, etc.) = false: they self-manage updates.
  bool needsParentRerender = false;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_COMPONENT_MODEL_H
