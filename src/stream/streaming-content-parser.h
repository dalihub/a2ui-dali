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

// Ported from AGenUI's StreamingContentParser — the central streaming pipeline.
// Modified: SurfaceCoordinator dependency replaced with IStreamEventSink callback interface.

#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include "protocol-stream-extractor.h"
#include "markdown-stream-plugin.h"
#include "text-stream-plugin.h"
#include "composite-stream-plugin.h"
#include "stream-event-sink.h"
#include <atomic>
#include <memory>


namespace A2ui {

/**
 * @brief Streaming content parser (formerly SessionManager)
 *
 * Handles streaming data reception, parsing, and forwarding.
 *
 * Supported operations:
 * 1. createSurface
 * 2. updateComponents (with streaming support for chunked reception)
 * 3. updateDataModel
 * 4. deleteSurface
 */
class StreamingContentParser {
public:
    /**
     * @brief Constructor.
     * @param sink IStreamEventSink pointer (not owned)
     */
    explicit StreamingContentParser(IStreamEventSink* sink);
    ~StreamingContentParser();

    bool start();
    void stop();
    bool isRunning() const { return _isRunning.load(); }

    void setQueryContent(const std::string &content);

    void processDataBeginning();
    void processDataAssembling(const std::string& data);
    void processDataEnding();

private:
    void processNormalEvent(const ProtocolStreamExtractor::ParseResult& result);
    void sendSingleComponentUpdate(const std::string& componentJson, const std::string& surfaceId, const std::string& version);
    void sendBatchedComponentUpdate(const std::vector<ProtocolStreamExtractor::ParseResult>& results,
                                    size_t start, size_t end);
    /**
     * @brief Coalescing variant of dispatchParseResults.
     *        Merges contiguous ComponentUpdate results that share the same
     *        surfaceId into a single updateComponents call so the
     *        downstream BatchScope folds N full-tree layouts into 1.
     *        Falls back to sendSingleComponentUpdate for size-1 runs.
     */
    void dispatchParseResultsBatched(const std::vector<ProtocolStreamExtractor::ParseResult>& results);
    void resetState();

    IStreamEventSink* _sink = nullptr;

    ProtocolStreamExtractor _extractor;
    std::unique_ptr<MarkdownStreamPlugin> _markdownPlugin;
    std::unique_ptr<TextStreamPlugin> _textPlugin;
    std::unique_ptr<CompositeStreamPlugin> _compositePlugin;

    std::string _queryContent;
    std::atomic_bool _isRunning{false};
};

} // namespace A2ui
