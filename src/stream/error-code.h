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

// Ported from AGenUI's agenui_errorcode_define.h

#ifndef A2UI_STREAM_ERROR_CODE_H
#define A2UI_STREAM_ERROR_CODE_H

#include <string>

namespace A2ui {

enum AGenUIExeCode {
    Execute_Success = 0,

    // CreateSurface
    CreateSurface_MissingKeyword,
    CreateSurface_MissingSurfaceId,
    CreateSurface_EmptySurfaceId,
    CreateSurface_JsonError,
    CreateSurface_DuplicateSurfaceId,

    // UpdateComponents
    UpdateComponents_MissingKeyword,
    UpdateComponents_MissingSurfaceId,
    UpdateComponents_EmptySurfaceId,
    UpdateComponents_SurfaceNotFound,
    UpdateComponents_JsonError,
    UpdateComponents_MissingComponentsField,
    UpdateComponents_ComponentsNotArray,
    UpdateComponents_MissingComponentEntity,
    UpdateComponents_TemplateExpansionFailed,

    // UpdateDataModel
    UpdateDataModel_MissingKeyword,
    UpdateDataModel_MissingSurfaceId,
    UpdateDataModel_EmptySurfaceId,
    UpdateDataModel_SurfaceNotFound,
    UpdateDataModel_JsonError,

    // AppendDataModel
    AppendDataModel_MissingKeyword,
    AppendDataModel_MissingSurfaceId,
    AppendDataModel_EmptySurfaceId,
    AppendDataModel_SurfaceNotFound,
    AppendDataModel_JsonError,

    // DeleteSurface
    DeleteSurface_MissingKeyword,
    DeleteSurface_MissingSurfaceId,
    DeleteSurface_EmptySurfaceId,
    DeleteSurface_JsonError,
};

std::string getExeCodeString(AGenUIExeCode code);

} // namespace A2ui

#endif // A2UI_STREAM_ERROR_CODE_H
