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

#pragma once

#include <string>
#include <vector>

namespace A2ui {

class IStreamPlugin;

/**
 * @brief Protocol streaming data parser
 *
 * Implements the full state machine for parsing streaming protocol data:
 * - Manages the data buffer, supporting accumulation of fragmented chunks
 * - Drives event detection, JSON completeness checking, and streaming component extraction for updateComponents
 * - Exposes a parse-result list to callers without exposing internal state-machine details
 *
 * Supported event types: createSurface, updateComponents (streaming), updateDataModel, deleteSurface
 */
class ProtocolStreamExtractor {
public:
    enum class EventType {
        Unknown,
        CreateSurface,
        UpdateComponents,
        UpdateDataModel,
        AppendDataModel,
        DeleteSurface
    };

    /**
     * @brief Single parse result
     *
     * NormalEvent: a complete non-streaming event (createSurface / updateDataModel / deleteSurface)
     * ComponentUpdate: a single component object from updateComponents
     */
    struct ParseResult {
        enum class Type { NormalEvent, ComponentUpdate };
        Type type = Type::NormalEvent;
        EventType eventType = EventType::Unknown;
        std::string eventJson;      // Valid for NormalEvent: complete event JSON
        std::string componentJson;  // Valid for ComponentUpdate: single component JSON
        std::string surfaceId;      // Valid for ComponentUpdate
        std::string version;        // Valid for ComponentUpdate
    };

    ProtocolStreamExtractor() = default;

    /**
     * @brief Sets the streaming parse plugin (optional).
     * Pass nullptr to disable the plugin and restore default behavior.
     * @param plugin Plugin pointer; lifetime managed by the caller.
     */
    void setPlugin(IStreamPlugin* plugin);

    /**
     * @brief Appends newly arrived streaming data to the internal buffer.
     * @param data New data chunk.
     */
    void appendData(const std::string& data);
    std::vector<ParseResult> driveParser();
    bool hasUnprocessedData() const;
    void reset();

    /**
     * @brief Extracts the first complete JSON object from the given buffer (static).
     * @param buffer Input buffer.
     * @param outJson Output: the extracted complete JSON string.
     * @param outEndPos Output: position immediately after the JSON end.
     * @return Whether a complete JSON was successfully extracted.
     */
    static bool extractFirstCompleteJson(const std::string& buffer, std::string& outJson, size_t& outEndPos);

    /**
     * @brief Checks whether a JSON object is fully closed (static).
     * @param json JSON string.
     * @param startPos Start position.
     * @param endPos Output: end position.
     * @return Whether the object is fully closed.
     */
    static bool isJsonObjectComplete(const std::string& json, size_t startPos, size_t& endPos);

    /**
     * @brief Detects the event type of a (possibly partial) message from its top-level key.
     * Only the members of the outermost object are inspected, so a value that merely
     * mentions another message name (e.g. "value":"createSurface") does not change the type.
     * @param data JSON data.
     * @return Event type, or Unknown when no known top-level key has arrived yet.
     */
    EventType detectEventType(const std::string& data) const;

    /**
     * @brief Returns the largest position <= endPos at which the JSON string content that
     * starts at fromPos can be cut without splitting an escape sequence (\x, \uXXXX, a
     * \uD800-\uDBFF high surrogate and its low half) or a raw UTF-8 multi-byte sequence.
     * fromPos must itself be such a boundary (e.g. right after the opening quote).
     */
    static size_t findSafeStringCut(const std::string& buffer, size_t fromPos, size_t endPos);

private:
    enum class StreamState {
        Idle,
        StreamingComponents,
        StreamingDataModel
    };

    /**
     * @brief DataModel streaming parse stack frame.
     * Tracks nesting depth during recursive parsing of a value object.
     */
    struct DmStackEntry {
        enum class Type { Object, Array };
        Type type;
        std::string pathPrefix;   // Accumulated path to reach this level
        int arrayIndex;           // Current element index in Array mode

        DmStackEntry(Type t, const std::string& path, int idx = 0)
            : type(t), pathPrefix(path), arrayIndex(idx) {}
    };

    /**
     * @brief One member of a JSON object as seen by scanObjectMembers().
     */
    struct JsonMember {
        std::string key;                       // Raw (still JSON-escaped) key text
        size_t valueStart = std::string::npos; // First byte of the value; npos if not arrived yet
        size_t valueEnd = 0;                   // Last byte of the value; valid when complete
        bool complete = false;                 // Whether the whole value has arrived
    };

    // State-machine main loop (processes all complete events)
    void processAllCompleteEvents(std::vector<ParseResult>& outResults);
    // Streaming component extraction loop
    void processStreamingData(std::vector<ParseResult>& outResults);
    // Initializes updateComponents streaming (extracts surfaceId/version, locates components array)
    bool startStreamingComponents(bool messageComplete);
    // Consumes the rest of an updateComponents message once its components array is closed
    void finishComponentsMessage();

    // === Message framing ===
    void discardUntilMessageStart();
    void consumeMessage(size_t endPos);
    void abandonMessageIfComplete(const char* reason);
    EventType detectEventTypeAt(const std::string& data, size_t objStart) const;
    bool scanObjectMembers(const std::string& buffer, size_t objStart,
                           std::vector<JsonMember>& outMembers, size_t& outClosePos) const;
    static bool findObjectEnd(const std::string& buffer, size_t fromPos, int depth, size_t& outEndPos);

    // === DataModel streaming ===
    bool startStreamingDataModel();
    void processStreamingDataModel(std::vector<ParseResult>& outResults);
    bool consumeDataModelValue(const std::string& path, size_t pos, std::vector<ParseResult>& outResults);
    void finishDataModelMessage();
    void emitDataModelUpdate(const std::string& path, const std::string& rawValue,
                             std::vector<ParseResult>& outResults);
    std::string buildDataModelEventJson(const char* eventKey, const std::string& path,
                                        const std::string& rawValue) const;
    void resetDataModelStreamState();

    // === Field-level incremental streaming for Markdown (sub-process embedded in DM streaming) ===
    void startMarkdownFieldStreaming(const std::string& fieldPath, size_t valueQuotePos,
                                      std::vector<ParseResult>& outResults);
    bool continueMarkdownFieldStreaming(std::vector<ParseResult>& outResults);
    void emitAppendDataModel(const std::string& path, const std::string& delta,
                             std::vector<ParseResult>& outResults);

    // JSON parsing low-level methods
    bool extractNextComponent(std::string& outComponent);
    bool isJsonArrayComplete(const std::string& json, size_t startPos, size_t& endPos) const;
    bool extractCompleteJsonValue(const std::string& buffer, size_t startPos, size_t& outEndPos) const;
    bool findClosingQuote(const std::string& buffer, size_t startPos, size_t& outEndPos) const;
    bool isValidClosingQuote(const std::string& buffer, size_t quotePos) const;
    bool extractJsonStringAt(const std::string& buffer, size_t pos, std::string& outStr, size_t& outEndPos) const;
    bool rawStringValue(const std::string& buffer, const JsonMember& member, std::string& outRaw) const;
    const char* getEventTypeName(EventType type) const;

    std::string _dataBuffer;      // Starts at the current message's '{' while streaming
    size_t _parsePosition = 0;
    std::string _surfaceId;       // Raw (JSON-escaped) surfaceId of the streamed updateComponents
    std::string _version;         // Raw (JSON-escaped) version of the streamed updateComponents
    bool _componentsArrayClosed = false;
    StreamState _streamState = StreamState::Idle;
    IStreamPlugin* _plugin = nullptr;  // Optional streaming parse plugin

    // DataModel streaming state
    std::string _dmSurfaceId;     // Decoded surfaceId
    std::string _dmBasePath;      // Decoded "path" of the message
    std::string _dmVersion;       // Decoded top-level "version"
    bool _dmHasVersion = false;
    bool _dmValuePending = false; // The top-level "value" has not been consumed yet
    size_t _dmParsePosition = 0;
    size_t _dmValueStart = 0;
    std::vector<DmStackEntry> _dmStack;

    // Field-level incremental streaming state for Markdown (embedded in DM streaming)
    bool _dmMarkdownStreaming = false;       // Whether a Markdown field is currently being streamed
    std::string _dmMarkdownFieldPath;        // Full path of the field being streamed
    size_t _dmMarkdownValueStart = 0;        // Start position of the string value content (after opening ")
    size_t _dmMarkdownLastSentEnd = 0;       // End position of the last sent content
};

} // namespace A2ui
