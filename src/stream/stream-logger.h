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

// Lightweight logging macros for the stream module.
// Ported from AGenUI's agenui_logger_internal.h.
// Can be adapted to the host project's logging system later.

#ifndef A2UI_STREAM_LOGGER_H
#define A2UI_STREAM_LOGGER_H

#include <cstdio>

// Map AGenUI log macros to simple fprintf for now.
// Replace these with the host project's logging system when integrating.
#define AGENUI_LOG(fmt, ...) do { fprintf(stderr, "[Stream] " fmt "\n", ##__VA_ARGS__); } while(0)
#define AGENUI_PERFORMANCE_LOG(tag, fmt, ...) do { } while(0)  // no-op by default

// Verbose per-chunk tracing; compiled in only with -DA2UI_STREAM_DEBUG.
#ifdef A2UI_STREAM_DEBUG
#define AGENUI_DEBUG_LOG(fmt, ...) AGENUI_LOG(fmt, ##__VA_ARGS__)
#else
#define AGENUI_DEBUG_LOG(fmt, ...) do { } while(0)
#endif

#endif // A2UI_STREAM_LOGGER_H
