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

#include "data-context.h"

namespace A2ui
{

DataContext::DataContext(DataModel& dm, const std::string& scope)
: mDataModel(dm),
  mScope(scope)
{
}

std::string DataContext::Resolve(const std::string& path) const
{
  if(path.empty())
  {
    return mScope;
  }

  // Absolute path
  if(path[0] == '/')
  {
    return path;
  }

  // Relative path: append to scope
  if(mScope == "/" || mScope.empty())
  {
    return "/" + path;
  }

  return mScope + "/" + path;
}

DataContext DataContext::CreateChildContext(const std::string& childScope) const
{
  return DataContext(mDataModel, childScope);
}

DataContext DataContext::CreateCollectionItemContext(const std::string& childScope,
                                                     int                index) const
{
  DataContext child(mDataModel, childScope);
  child.mCollectionIndex = index;
  return child;
}

DataContext DataContext::CreateChildContextForIndex(int index) const
{
  std::string childScope = mScope;
  if(childScope == "/")
  {
    childScope = "/" + std::to_string(index);
  }
  else
  {
    childScope += "/" + std::to_string(index);
  }
  DataContext child(mDataModel, childScope);
  child.mCollectionIndex = index;
  return child;
}

std::string DataContext::GetString(const std::string& path) const
{
  return mDataModel.GetString(Resolve(path));
}

float DataContext::GetFloat(const std::string& path, float fallback) const
{
  return mDataModel.GetFloat(Resolve(path), fallback);
}

bool DataContext::GetBool(const std::string& path, bool fallback) const
{
  return mDataModel.GetBool(Resolve(path), fallback);
}

const Dali::Ui::Integration::TreeNode* DataContext::ResolvePath(const std::string& path) const
{
  return mDataModel.ResolvePath(Resolve(path));
}

bool DataContext::SetValue(const std::string& path, const std::string& value)
{
  return mDataModel.SetValue(Resolve(path), value);
}

bool DataContext::SetBoolValue(const std::string& path, bool value)
{
  return mDataModel.SetBoolValue(Resolve(path), value);
}

uint32_t DataContext::Watch(const std::string& path, DataChangeCallback cb)
{
  return mDataModel.Watch(Resolve(path), std::move(cb));
}

} // namespace A2ui
