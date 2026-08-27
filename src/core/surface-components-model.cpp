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

#include "surface-components-model.h"
#include "a2ui-sanitizer.h"
#include <dali/integration-api/debug.h>

using Dali::Ui::Integration::TreeNode;

namespace A2ui
{

void SurfaceComponentsModel::AddComponents(const TreeNode& componentsArray)
{
  if(componentsArray.GetType() != TreeNode::ARRAY)
  {
    DALI_LOG_ERROR("[A2UI] AddComponents: expected ARRAY\n");
    return;
  }

  mLastModifiedIds.clear();

  for(auto it = componentsArray.CBegin(); it != componentsArray.CEnd(); ++it)
  {
    const TreeNode& compNode = (*it).second;

    // Extract id
    const TreeNode* idNode = compNode.Find("id");
    if(!idNode || idNode->GetType() != TreeNode::STRING)
    {
      DALI_LOG_ERROR("[A2UI] Component missing 'id' field, skipping\n");
      continue;
    }

    ComponentModel comp;
    comp.id      = idNode->GetString();
    comp.rawNode = &compNode;

    // Phase 4: Validate component ID
    if(!A2uiSanitizer::ValidateComponentId(comp.id))
    {
      DALI_LOG_ERROR("[A2UI] Component ID '%s' failed sanitization, skipping\n", comp.id.c_str());
      continue;
    }

    // Extract component type — A2UI standard uses "component", also support "type" as alias
    const TreeNode* typeNode = compNode.Find("component");
    if(!typeNode)
    {
      typeNode = compNode.Find("type");
    }
    if(typeNode && typeNode->GetType() == TreeNode::STRING)
    {
      comp.type = typeNode->GetString();
    }

    // Extract children (array of string IDs)
    const TreeNode* childrenNode = compNode.Find("children");
    if(childrenNode && childrenNode->GetType() == TreeNode::ARRAY)
    {
      for(auto cit = childrenNode->CBegin(); cit != childrenNode->CEnd(); ++cit)
      {
        const TreeNode& childRef = (*cit).second;
        if(childRef.GetType() == TreeNode::STRING)
        {
          comp.childIds.push_back(childRef.GetString());
        }
      }
    }

    // Extract child (single string ID for Card, Button, etc.)
    const TreeNode* childNode = compNode.Find("child");
    if(childNode && childNode->GetType() == TreeNode::STRING)
    {
      comp.childId = childNode->GetString();
    }

    // Track modified IDs
    mLastModifiedIds.push_back(comp.id);

    // Maintain parent reverse index: each child's ID → this component's ID.
    for(const auto& cid : comp.childIds)
    {
      mParentMap[cid] = comp.id;
    }
    if(!comp.childId.empty())
    {
      mParentMap[comp.childId] = comp.id;
    }

    // Upsert into map
    mComponents[comp.id] = std::move(comp);
  }
}


const ComponentModel* SurfaceComponentsModel::GetComponent(const std::string& id) const
{
  auto it = mComponents.find(id);
  if(it != mComponents.end())
  {
    return &it->second;
  }
  return nullptr;
}

const ComponentModel* SurfaceComponentsModel::GetRoot() const
{
  return GetComponent("root");
}

void SurfaceComponentsModel::Clear()
{
  mComponents.clear();
  mParentMap.clear();
}

std::string SurfaceComponentsModel::GetParentId(const std::string& childId) const
{
  auto it = mParentMap.find(childId);
  if(it != mParentMap.end())
  {
    return it->second;
  }
  return "";
}

int SurfaceComponentsModel::GetChildIndexInParent(const std::string& childId) const
{
  std::string parentId = GetParentId(childId);
  if(parentId.empty()) return -1;

  const ComponentModel* parent = GetComponent(parentId);
  if(!parent) return -1;

  for(int i = 0; i < static_cast<int>(parent->childIds.size()); ++i)
  {
    if(parent->childIds[i] == childId) return i;
  }

  // Single child (Card/Button childId)
  if(parent->childId == childId) return 0;

  return -1;
}

} // namespace A2ui

