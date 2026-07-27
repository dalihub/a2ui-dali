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

#include "surface-group-model.h"
#include "a2ui-protocol.h"
#include <algorithm>
#include <sstream>

namespace A2ui
{

SurfaceGroupModel::SurfaceGroupModel()
{
}

SurfaceModel& SurfaceGroupModel::GetOrCreateSurface(const std::string& surfaceId,
                                                      const std::string& catalogId)
{
  auto it = mSurfaces.find(surfaceId);
  if(it == mSurfaces.end())
  {
    auto [insertIt, ok] = mSurfaces.emplace(surfaceId, SurfaceModel());
    insertIt->second.Create(surfaceId, catalogId);

    if(mOnSurfaceChange) mOnSurfaceChange(surfaceId, true);
    return insertIt->second;
  }
  return it->second;
}

SurfaceModel* SurfaceGroupModel::GetSurface(const std::string& surfaceId)
{
  auto it = mSurfaces.find(surfaceId);
  return (it != mSurfaces.end()) ? &it->second : nullptr;
}

const SurfaceModel* SurfaceGroupModel::GetSurface(const std::string& surfaceId) const
{
  auto it = mSurfaces.find(surfaceId);
  return (it != mSurfaces.end()) ? &it->second : nullptr;
}

void SurfaceGroupModel::DeleteSurface(const std::string& surfaceId)
{
  auto it = mSurfaces.find(surfaceId);
  if(it != mSurfaces.end())
  {
    it->second.Delete();
    mSurfaces.erase(it);
    mPlacements.erase(surfaceId);

    if(mOnSurfaceChange) mOnSurfaceChange(surfaceId, false);
  }
}

bool SurfaceGroupModel::HasSurface(const std::string& surfaceId) const
{
  return mSurfaces.find(surfaceId) != mSurfaces.end();
}

std::string SurfaceGroupModel::GetClientDataModel() const
{
  // Deterministic order: the map is unordered, and a payload whose key order changes
  // between two otherwise identical sends is needless churn for anything diffing it.
  std::vector<std::string> ids;
  for(const auto& [id, surface] : mSurfaces)
  {
    if(surface.IsCreated() && surface.GetSendDataModel()) ids.push_back(id);
  }
  if(ids.empty()) return {};
  std::sort(ids.begin(), ids.end());

  std::ostringstream j;
  j << "{\"version\":\"" << A2UI_PROTOCOL_VERSION << "\",\"surfaces\":{";
  bool first = true;
  for(const std::string& id : ids)
  {
    const SurfaceModel& surface = mSurfaces.at(id);
    std::string dm = surface.GetDataModel().Serialize();
    if(dm.empty()) dm = "{}"; // a surface that has not received data yet still reports in
    if(!first) j << ",";
    first = false;
    j << "\"" << id << "\":" << dm;
  }
  j << "}}";
  return j.str();
}

std::vector<std::string> SurfaceGroupModel::GetSurfaceIds() const
{
  std::vector<std::string> ids;
  ids.reserve(mSurfaces.size());
  for(const auto& [id, surface] : mSurfaces)
  {
    ids.push_back(id);
  }
  return ids;
}

SurfaceGroupModel::SurfacePlacement SurfaceGroupModel::DeterminePlacement(
  const std::string& surfaceId) const
{
  // Check explicit placement first
  auto it = mPlacements.find(surfaceId);
  if(it != mPlacements.end())
  {
    return it->second;
  }

  // Rule-based placement from surfaceId naming
  if(surfaceId == "sidebar" || surfaceId == "nav" ||
     surfaceId.find("sidebar") != std::string::npos)
  {
    return {Zone::SIDEBAR, 0.3f};
  }
  if(surfaceId == "notification" || surfaceId == "toast" ||
     surfaceId.find("notification") != std::string::npos)
  {
    return {Zone::NOTIFICATION, 0.0f};
  }
  if(surfaceId.find("overlay") != std::string::npos ||
     surfaceId.find("modal") != std::string::npos)
  {
    return {Zone::OVERLAY, 0.0f};
  }

  return {Zone::MAIN, 1.0f};
}

void SurfaceGroupModel::SetPlacement(const std::string& surfaceId, SurfacePlacement placement)
{
  mPlacements[surfaceId] = placement;
}

std::vector<std::string> SurfaceGroupModel::GetSurfacesInZone(Zone zone) const
{
  std::vector<std::string> result;
  for(const auto& [id, surface] : mSurfaces)
  {
    SurfacePlacement placement = DeterminePlacement(id);
    if(placement.zone == zone)
    {
      result.push_back(id);
    }
  }
  return result;
}

void SurfaceGroupModel::Reset()
{
  for(auto& [id, surface] : mSurfaces)
  {
    surface.Reset();
  }
  mSurfaces.clear();
  mPlacements.clear();
}

} // namespace A2ui
