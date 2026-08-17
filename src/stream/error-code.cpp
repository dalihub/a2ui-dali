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

// Ported from AGenUI's agenui_errorcode_define.cpp

#include "error-code.h"

namespace A2ui {

std::string getExeCodeString(AGenUIExeCode code) {
    switch (code) {
        case Execute_Success:
            return "execute success";

        // CreateSurface
        case CreateSurface_MissingKeyword:
            return "missing createSurface keyword";
        case CreateSurface_MissingSurfaceId:
            return "missing surfaceId in createSurface";
        case CreateSurface_EmptySurfaceId:
            return "surfaceId is empty in createSurface";
        case CreateSurface_JsonError:
            return "JSON exception in createSurface";
        case CreateSurface_DuplicateSurfaceId:
            return "surfaceId already exists";

        // UpdateComponents
        case UpdateComponents_MissingKeyword:
            return "missing updateComponents keyword";
        case UpdateComponents_MissingSurfaceId:
            return "missing surfaceId in updateComponents";
        case UpdateComponents_EmptySurfaceId:
            return "surfaceId is empty in updateComponents";
        case UpdateComponents_SurfaceNotFound:
            return "surface not found in updateComponents";
        case UpdateComponents_JsonError:
            return "JSON exception in updateComponents";
        case UpdateComponents_MissingComponentsField:
            return "missing components field in updateComponents";
        case UpdateComponents_ComponentsNotArray:
            return "components is not an array in updateComponents";
        case UpdateComponents_MissingComponentEntity:
            return "missing component entity in updateComponents";
        case UpdateComponents_TemplateExpansionFailed:
            return "template expansion failed in updateComponents";

        // UpdateDataModel
        case UpdateDataModel_MissingKeyword:
            return "missing updateDataModel keyword";
        case UpdateDataModel_MissingSurfaceId:
            return "missing surfaceId in updateDataModel";
        case UpdateDataModel_EmptySurfaceId:
            return "surfaceId is empty in updateDataModel";
        case UpdateDataModel_SurfaceNotFound:
            return "surface not found in updateDataModel";
        case UpdateDataModel_JsonError:
            return "JSON exception in updateDataModel";

        // AppendDataModel
        case AppendDataModel_MissingKeyword:
            return "missing appendDataModel keyword";
        case AppendDataModel_MissingSurfaceId:
            return "missing surfaceId in appendDataModel";
        case AppendDataModel_EmptySurfaceId:
            return "surfaceId is empty in appendDataModel";
        case AppendDataModel_SurfaceNotFound:
            return "surface not found in appendDataModel";
        case AppendDataModel_JsonError:
            return "JSON exception in appendDataModel";

        // DeleteSurface
        case DeleteSurface_MissingKeyword:
            return "missing deleteSurface keyword";
        case DeleteSurface_MissingSurfaceId:
            return "missing surfaceId in deleteSurface";
        case DeleteSurface_EmptySurfaceId:
            return "surfaceId is empty in deleteSurface";
        case DeleteSurface_JsonError:
            return "JSON exception in deleteSurface";

        default:
            return "unknown";
    }
}

} // namespace A2ui
