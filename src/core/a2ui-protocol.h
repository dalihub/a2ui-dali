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

#ifndef DALI_GENERATIVE_UI_A2UI_PROTOCOL_H
#define DALI_GENERATIVE_UI_A2UI_PROTOCOL_H

#include <cstring>

namespace A2ui
{

/**
 * Wire-level protocol constants shared by the message layer and the transports.
 *
 * Kept here (rather than inside a transport) because these are properties of the
 * A2UI protocol itself: every outgoing envelope carries the version string, and
 * every transport that frames A2UI payloads uses the same media type.
 */

/// Protocol version stamped on every outgoing envelope.
/// "v0.9" validates against both the v0.9 schema (`const`) and the v0.9.1 schema
/// (`enum: ["v0.9", "v0.9.1"]`), so it is the safest value to emit while the
/// renderer targets the v0.9 family.
constexpr const char* A2UI_PROTOCOL_VERSION = "v0.9";

/// Canonical media type for A2UI payloads, per the IANA `application/<x>+json`
/// structured-suffix convention.
constexpr const char* A2UI_MIME_TYPE = "application/a2ui+json";

/// Pre-IANA spelling. Still accepted on receive so agents that have not migrated
/// keep working; never emitted.
constexpr const char* A2UI_MIME_TYPE_LEGACY = "application/json+a2ui";

/**
 * True if @p mimeType labels an A2UI payload.
 * A null pointer means "unlabelled", which callers treat as A2UI by convention.
 */
inline bool IsA2uiMimeType(const char* mimeType)
{
  if(!mimeType) return true;
  return std::strcmp(mimeType, A2UI_MIME_TYPE) == 0 ||
         std::strcmp(mimeType, A2UI_MIME_TYPE_LEGACY) == 0;
}

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_A2UI_PROTOCOL_H
