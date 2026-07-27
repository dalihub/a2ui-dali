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

#ifndef DALI_GENERATIVE_UI_SURFACE_GROUP_MODEL_H
#define DALI_GENERATIVE_UI_SURFACE_GROUP_MODEL_H

#include "surface-model.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

namespace A2ui
{

/**
 * Manages multiple surfaces simultaneously.
 * Each surface is identified by a unique surfaceId and placed in a zone.
 */
class SurfaceGroupModel
{
public:
  enum class Zone
  {
    MAIN,
    SIDEBAR,
    OVERLAY,
    NOTIFICATION
  };

  struct SurfacePlacement
  {
    Zone zone = Zone::MAIN;
    float weight = 1.0f;  // relative size within zone
  };

  using SurfaceChangeCallback = std::function<void(const std::string& surfaceId, bool created)>;

  SurfaceGroupModel();

  /**
   * Create or get a surface by ID.
   * If the surface doesn't exist, it is created.
   */
  SurfaceModel& GetOrCreateSurface(const std::string& surfaceId,
                                    const std::string& catalogId = "basic");

  /**
   * Get an existing surface by ID.
   * @return pointer to SurfaceModel, or nullptr if not found
   */
  SurfaceModel* GetSurface(const std::string& surfaceId);
  const SurfaceModel* GetSurface(const std::string& surfaceId) const;

  /**
   * Delete a surface by ID.
   */
  void DeleteSurface(const std::string& surfaceId);

  /**
   * Check if a surface exists.
   */
  bool HasSurface(const std::string& surfaceId) const;

  /**
   * Get the list of all surface IDs.
   */
  std::vector<std::string> GetSurfaceIds() const;

  /**
   * Get the number of surfaces.
   */
  size_t GetSurfaceCount() const { return mSurfaces.size(); }

  /**
   * The `a2uiClientDataModel` payload for the surfaces that asked for Data Model Sync.
   *
   * A surface created with `sendDataModel: true` expects its current data model to travel
   * back with every client-to-server message, so the agent can read what the user has
   * typed. The transport calls this before sending and, if the result is non-empty, drops
   * it into its metadata field.
   *
   * @return the serialized `{"version":…,"surfaces":{…}}` object, or an empty string when
   *         no active surface enabled the flag (nothing should then be attached).
   */
  std::string GetClientDataModel() const;

  /**
   * Determine placement zone for a surface based on its ID or hints.
   * Default rules:
   *   - "main" or "default" → MAIN
   *   - "sidebar" or "nav" → SIDEBAR
   *   - "notification" or "toast" → NOTIFICATION
   *   - others → MAIN
   */
  SurfacePlacement DeterminePlacement(const std::string& surfaceId) const;

  /**
   * Set an explicit placement for a surface.
   */
  void SetPlacement(const std::string& surfaceId, SurfacePlacement placement);

  /**
   * Get all surfaces in a given zone.
   */
  std::vector<std::string> GetSurfacesInZone(Zone zone) const;

  /**
   * Reset all surfaces.
   */
  void Reset();

  /**
   * Set callback for surface creation/deletion events.
   */
  void SetOnSurfaceChange(SurfaceChangeCallback cb) { mOnSurfaceChange = std::move(cb); }

private:
  std::unordered_map<std::string, SurfaceModel> mSurfaces;
  std::unordered_map<std::string, SurfacePlacement> mPlacements;
  SurfaceChangeCallback mOnSurfaceChange;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_SURFACE_GROUP_MODEL_H
