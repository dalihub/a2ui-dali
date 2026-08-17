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

// New interface to decouple StreamingContentParser from SurfaceCoordinator.
// The host project implements this interface to receive parsed A2UI events.

#ifndef A2UI_STREAM_EVENT_SINK_H
#define A2UI_STREAM_EVENT_SINK_H

#include <string>
#include "error-code.h"

namespace A2ui {

class IStreamEventSink {
public:
    virtual ~IStreamEventSink() = default;
    virtual AGenUIExeCode createSurface(const std::string& jsonData) = 0;
    virtual AGenUIExeCode deleteSurface(const std::string& jsonData) = 0;
    virtual AGenUIExeCode updateComponents(const std::string& jsonData) = 0;
    virtual AGenUIExeCode updateDataModel(const std::string& jsonData) = 0;
    virtual AGenUIExeCode appendDataModel(const std::string& jsonData) = 0;
};

} // namespace A2ui

#endif // A2UI_STREAM_EVENT_SINK_H
