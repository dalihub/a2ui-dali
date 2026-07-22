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

#include "view-pool.h"
#include <dali/integration-api/debug.h>

using Dali::Ui::View;

namespace A2ui
{

ViewPool::ViewPool(size_t maxPerType)
  : mMaxPerType(maxPerType)
{
}

View ViewPool::Acquire(const std::string& componentType)
{
  auto it = mPool.find(componentType);
  if(it != mPool.end() && !it->second.empty())
  {
    View view = it->second.back();
    it->second.pop_back();
    return view;
  }
  return View();
}

void ViewPool::Release(const std::string& componentType, View view)
{
  if(!view) return;

  // Unparent the view
  auto parent = view.GetParent();
  if(parent)
  {
    parent.Remove(view);
  }

  // Reset common properties to avoid stale state
  view.SetProperty(Dali::Actor::Property::OPACITY, 1.0f);
  view.SetProperty(Dali::Actor::Property::POSITION, Dali::Vector3::ZERO);
  view.SetProperty(Dali::Actor::Property::SCALE, Dali::Vector3::ONE);
  view.SetProperty(Dali::Actor::Property::VISIBLE, true);

  // Remove all children
  while(view.GetChildViewCount() > 0)
  {
    view.Remove(view.GetChildViewAt(0u));
  }

  auto& pool = mPool[componentType];
  if(pool.size() < mMaxPerType)
  {
    pool.push_back(view);
  }
  // else: discard (pool full)
}

void ViewPool::Clear()
{
  mPool.clear();
}

size_t ViewPool::GetPooledCount(const std::string& componentType) const
{
  auto it = mPool.find(componentType);
  if(it != mPool.end())
  {
    return it->second.size();
  }
  return 0;
}

size_t ViewPool::GetTotalPooled() const
{
  size_t total = 0;
  for(const auto& [type, views] : mPool)
  {
    total += views.size();
  }
  return total;
}

} // namespace A2ui
