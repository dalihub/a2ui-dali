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

#include "protocol-stream-extractor.h"
#include "stream-plugin-interface.h"
#include "stream-logger.h"
#include "nlohmann/json.hpp"

namespace A2ui {

namespace {

inline bool isJsonWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Decodes a raw (still JSON-escaped) string body. Returns false if it is not a valid JSON string.
bool decodeJsonString(const std::string& raw, std::string& out) {
    nlohmann::json j = nlohmann::json::parse("\"" + raw + "\"", nullptr, false);
    if (j.is_discarded() || !j.is_string()) {
        return false;
    }
    out = j.get<std::string>();
    return true;
}

// Encodes a string as a JSON string literal (with quotes).
std::string quoteJsonString(const std::string& s) {
    return nlohmann::json(s).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// Appends one reference token to a JSON pointer, escaping '~' and '/' (RFC 6901).
std::string appendPointerToken(const std::string& prefix, const std::string& token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (char c : token) {
        if (c == '~') {
            escaped += "~0";
        } else if (c == '/') {
            escaped += "~1";
        } else {
            escaped += c;
        }
    }
    if (prefix.empty() || prefix == "/") {
        return "/" + escaped;
    }
    return prefix + "/" + escaped;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

void ProtocolStreamExtractor::setPlugin(IStreamPlugin* plugin) {
    _plugin = plugin;
}

static constexpr size_t MAX_BUFFER_BYTES = 10 * 1024 * 1024; // 10 MB

void ProtocolStreamExtractor::appendData(const std::string& data) {
    if (_dataBuffer.size() + data.size() > MAX_BUFFER_BYTES) {
        // Dropping the chunk and keeping the buffer would later splice unrelated bytes into the
        // pending message. Discard the pending message instead and resynchronise on the next '{'.
        AGENUI_LOG("error: stream buffer limit exceeded (buffer=%zu + incoming=%zu > limit=%zu); "
            "discarding the pending message and resetting the parser",
            _dataBuffer.size(), data.size(), MAX_BUFFER_BYTES);
        reset();
        if (data.size() > MAX_BUFFER_BYTES) {
            return;
        }
    }
    _dataBuffer += data;
}

std::vector<ProtocolStreamExtractor::ParseResult> ProtocolStreamExtractor::driveParser() {
    std::vector<ParseResult> results;

    if (_plugin) {
        if (_plugin->isComponentStreaming()) {
            size_t newPos = 0;
            bool still = _plugin->continueComponentStreaming(
                _dataBuffer, _surfaceId, _version, results, newPos);
            if (still) {
                return results;
            }
            _parsePosition = newPos;
        }
        if (_plugin->isDataModelStreaming()) {
            size_t endPos = 0;
            bool still = _plugin->continueDataModelStreaming(
                _dataBuffer, results, endPos);
            if (still) {
                return results;
            }
            _dataBuffer = (endPos < _dataBuffer.length())
                ? _dataBuffer.substr(endPos) : "";
        }
    }

    if (_streamState == StreamState::StreamingComponents) {
        AGENUI_DEBUG_LOG("In component streaming mode");
        processStreamingData(results);
    } else if (_streamState == StreamState::StreamingDataModel) {
        AGENUI_DEBUG_LOG("In DataModel streaming mode");
        processStreamingDataModel(results);
    }
    // Once the current message is finished, process whatever follows it in the buffer
    if (_streamState == StreamState::Idle) {
        processAllCompleteEvents(results);
    }
    return results;
}

bool ProtocolStreamExtractor::hasUnprocessedData() const {
    return !_dataBuffer.empty();
}

void ProtocolStreamExtractor::reset() {
    _dataBuffer.clear();
    _parsePosition = 0;
    _surfaceId.clear();
    _version.clear();
    _componentsArrayClosed = false;
    _streamState = StreamState::Idle;
    resetDataModelStreamState();
    if (_plugin) {
        _plugin->reset();
    }
}

// MARK: - State machine main loop

void ProtocolStreamExtractor::processAllCompleteEvents(std::vector<ParseResult>& outResults) {
    while (_streamState == StreamState::Idle) {
        discardUntilMessageStart();
        if (_dataBuffer.empty()) {
            return;
        }

        size_t endPos = 0;
        bool complete = isJsonObjectComplete(_dataBuffer, 0, endPos);
        EventType eventType = detectEventTypeAt(_dataBuffer, 0);

        if (eventType == EventType::UpdateComponents) {
            // Complete or not, components are delivered one by one from the message in place
            if (!startStreamingComponents(complete)) {
                if (!complete) {
                    return; // surfaceId / components array not available yet
                }
                AGENUI_LOG("discarding updateComponents without surfaceId or components array");
                consumeMessage(endPos);
                continue;
            }
            _streamState = StreamState::StreamingComponents;
            processStreamingData(outResults);
            continue; // Loop ends while the message is still streaming
        }

        if (!complete) {
            // Try field-level streaming for a partial updateDataModel; otherwise wait for more data
            if (eventType == EventType::UpdateDataModel && startStreamingDataModel()) {
                _streamState = StreamState::StreamingDataModel;
                processStreamingDataModel(outResults);
                continue;
            }
            return;
        }

        if (eventType == EventType::Unknown) {
            AGENUI_LOG("discarding message without a known A2UI message key");
            consumeMessage(endPos);
            continue;
        }

        // createSurface, updateDataModel, appendDataModel, deleteSurface: deliver the message as a whole
        ParseResult result;
        result.type = ParseResult::Type::NormalEvent;
        result.eventType = eventType;
        result.eventJson = _dataBuffer.substr(0, endPos + 1);
        outResults.emplace_back(std::move(result));
        consumeMessage(endPos);
    }
}

void ProtocolStreamExtractor::processStreamingData(std::vector<ParseResult>& outResults) {
    if (_componentsArrayClosed) {
        finishComponentsMessage();
        return;
    }

    std::string component;
    while (extractNextComponent(component)) {
        if (_plugin) {
            _plugin->onComponentExtracted(component);
        }

        ParseResult result;
        result.type = ParseResult::Type::ComponentUpdate;
        result.eventType = ProtocolStreamExtractor::EventType::UpdateComponents;
        result.componentJson = component;
        result.surfaceId = _surfaceId;
        result.version = _version;
        outResults.emplace_back(std::move(result));
    }

    if (_plugin &&
        _streamState == StreamState::StreamingComponents &&
        _parsePosition < _dataBuffer.length() &&
        _dataBuffer[_parsePosition] == '{') {
        if (_plugin->handleIncompleteComponent(
                _dataBuffer, _parsePosition,
                _surfaceId, _version, outResults)) {
            return; // Plugin takes over
        }
    }

    if (_streamState == StreamState::StreamingComponents && _parsePosition < _dataBuffer.length()) {
        char c = _dataBuffer[_parsePosition];
        if (c == ']') {
            _parsePosition++;
            _componentsArrayClosed = true;
            finishComponentsMessage();
        } else if (c != '{') {
            abandonMessageIfComplete("unexpected token in components array");
        }
    }
}

bool ProtocolStreamExtractor::startStreamingComponents(bool messageComplete) {
    std::vector<JsonMember> members;
    size_t closePos = 0;
    scanObjectMembers(_dataBuffer, 0, members, closePos);

    // Components are re-wrapped one by one with the message's version, so the version must be
    // known before streaming starts: either it precedes the payload or the message is complete.
    std::string version;
    bool versionSeen = false;
    const JsonMember* payload = nullptr;
    for (const auto& m : members) {
        if (m.key == "version") {
            if (!m.complete || !rawStringValue(_dataBuffer, m, version)) {
                return false;
            }
            versionSeen = true;
        } else if (m.key == "updateComponents" && !payload) {
            payload = &m;
            if (!messageComplete) {
                break;
            }
        }
    }
    if (!payload || payload->valueStart == std::string::npos || _dataBuffer[payload->valueStart] != '{') {
        return false;
    }
    if (!messageComplete && !versionSeen) {
        return false; // A version may still follow the payload; wait for the whole message
    }

    std::vector<JsonMember> inner;
    scanObjectMembers(_dataBuffer, payload->valueStart, inner, closePos);
    std::string surfaceId;
    bool haveSurfaceId = false;
    size_t componentsStart = std::string::npos;
    for (const auto& m : inner) {
        if (m.key == "surfaceId") {
            if (!m.complete || !rawStringValue(_dataBuffer, m, surfaceId)) {
                return false;
            }
            haveSurfaceId = true;
        } else if (m.key == "components" && componentsStart == std::string::npos) {
            if (m.valueStart == std::string::npos) {
                return false;
            }
            componentsStart = m.valueStart;
            if (!messageComplete) {
                break; // surfaceId must precede the array to stream it
            }
        }
    }
    if (!haveSurfaceId || surfaceId.empty() ||
        componentsStart == std::string::npos || _dataBuffer[componentsStart] != '[') {
        return false;
    }

    _surfaceId = surfaceId;
    _version = version;
    _parsePosition = componentsStart + 1;
    _componentsArrayClosed = false;
    return true;
}

void ProtocolStreamExtractor::finishComponentsMessage() {
    // After the components array we are inside updateComponents (depth 2). Other members may
    // follow before the two closing braces; skip them without assuming "]}}".
    size_t endPos = 0;
    if (!findObjectEnd(_dataBuffer, _parsePosition, 2, endPos)) {
        return; // Wait for the rest of the message
    }
    consumeMessage(endPos);
    _surfaceId.clear();
    _version.clear();
    _componentsArrayClosed = false;
    _streamState = StreamState::Idle;
}

// MARK: - Message framing

void ProtocolStreamExtractor::discardUntilMessageStart() {
    size_t pos = 0;
    while (pos < _dataBuffer.length() && isJsonWhitespace(_dataBuffer[pos])) {
        pos++;
    }
    if (pos < _dataBuffer.length() && _dataBuffer[pos] != '{') {
        // Bytes that cannot start a message (e.g. an SSE "data: " prefix or a stray token)
        size_t brace = _dataBuffer.find('{', pos);
        size_t dropEnd = (brace == std::string::npos) ? _dataBuffer.length() : brace;
        AGENUI_LOG("discarding %zu byte(s) that cannot start an A2UI message", dropEnd - pos);
        pos = dropEnd;
    }
    if (pos > 0) {
        _dataBuffer.erase(0, pos);
    }
}

void ProtocolStreamExtractor::consumeMessage(size_t endPos) {
    _dataBuffer.erase(0, endPos + 1);
    _parsePosition = 0;
}

void ProtocolStreamExtractor::abandonMessageIfComplete(const char* reason) {
    size_t endPos = 0;
    if (!isJsonObjectComplete(_dataBuffer, 0, endPos)) {
        return; // Wait; the message is discarded once it is complete
    }
    AGENUI_LOG("discarding malformed message (%s)", reason);
    consumeMessage(endPos);
    _surfaceId.clear();
    _version.clear();
    _componentsArrayClosed = false;
    resetDataModelStreamState();
    _streamState = StreamState::Idle;
}

// MARK: - JSON parsing low-level methods

bool ProtocolStreamExtractor::extractFirstCompleteJson(const std::string& buffer, std::string& outJson, size_t& outEndPos) {
    // Skip whitespace and any bytes that cannot start a message
    size_t startPos = buffer.find('{');
    if (startPos == std::string::npos) {
        return false;
    }

    size_t endPos = 0;
    if (isJsonObjectComplete(buffer, startPos, endPos)) {
        outJson = buffer.substr(startPos, endPos - startPos + 1);
        outEndPos = endPos + 1;
        return true;
    }

    return false;
}

bool ProtocolStreamExtractor::extractNextComponent(std::string& outComponent) {
    while (_parsePosition < _dataBuffer.length()) {
        char c = _dataBuffer[_parsePosition];
        if (!isJsonWhitespace(c) && c != ',') {
            break;
        }
        _parsePosition++;
    }

    if (_parsePosition >= _dataBuffer.length()) {
        return false;
    }

    if (_dataBuffer[_parsePosition] == ']') {
        // Do not advance _parsePosition; let processStreamingData handle it
        return false;
    }

    if (_dataBuffer[_parsePosition] != '{') {
        return false;
    }

    size_t endPos = 0;
    if (isJsonObjectComplete(_dataBuffer, _parsePosition, endPos)) {
        outComponent = _dataBuffer.substr(_parsePosition, endPos - _parsePosition + 1);
        _parsePosition = endPos + 1;
        return true;
    }

    // JSON object is incomplete; wait for more data
    return false;
}

bool ProtocolStreamExtractor::findObjectEnd(const std::string& buffer, size_t fromPos, int depth, size_t& outEndPos) {
    bool inString = false;
    bool escaped = false;

    for (size_t i = fromPos; i < buffer.length(); i++) {
        char c = buffer[i];

        if (escaped) {
            escaped = false;
            continue;
        }
        if (inString) {
            if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                outEndPos = i;
                return true;
            }
        }
    }

    return false;
}

bool ProtocolStreamExtractor::isJsonObjectComplete(const std::string& json, size_t startPos, size_t& endPos) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = startPos; i < json.length(); i++) {
        char c = json[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == '"') {
            inString = !inString;
            continue;
        }

        if (!inString) {
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    endPos = i;
                    return true;
                }
            }
        }
    }

    return false;
}

bool ProtocolStreamExtractor::scanObjectMembers(const std::string& buffer, size_t objStart,
                                                std::vector<JsonMember>& outMembers, size_t& outClosePos) const {
    outMembers.clear();
    if (objStart >= buffer.length() || buffer[objStart] != '{') {
        return false;
    }
    size_t pos = objStart + 1;
    while (true) {
        while (pos < buffer.length() && (isJsonWhitespace(buffer[pos]) || buffer[pos] == ',')) {
            pos++;
        }
        if (pos >= buffer.length()) {
            return false;
        }
        if (buffer[pos] == '}') {
            outClosePos = pos;
            return true;
        }
        JsonMember member;
        size_t keyEnd = 0;
        if (!extractJsonStringAt(buffer, pos, member.key, keyEnd)) {
            return false; // Key not complete yet (or not a key)
        }
        pos = keyEnd + 1;
        while (pos < buffer.length() && isJsonWhitespace(buffer[pos])) {
            pos++;
        }
        if (pos >= buffer.length() || buffer[pos] != ':') {
            if (pos >= buffer.length()) {
                outMembers.push_back(member);
            }
            return false;
        }
        pos++;
        while (pos < buffer.length() && isJsonWhitespace(buffer[pos])) {
            pos++;
        }
        if (pos >= buffer.length()) {
            outMembers.push_back(member);
            return false;
        }
        member.valueStart = pos;
        size_t valueEnd = 0;
        if (!extractCompleteJsonValue(buffer, pos, valueEnd)) {
            outMembers.push_back(member);
            return false;
        }
        member.valueEnd = valueEnd;
        member.complete = true;
        outMembers.push_back(member);
        pos = valueEnd + 1;
    }
}

bool ProtocolStreamExtractor::rawStringValue(const std::string& buffer, const JsonMember& member, std::string& outRaw) const {
    if (!member.complete || buffer[member.valueStart] != '"' || member.valueEnd <= member.valueStart) {
        return false;
    }
    outRaw = buffer.substr(member.valueStart + 1, member.valueEnd - member.valueStart - 1);
    return true;
}

ProtocolStreamExtractor::EventType ProtocolStreamExtractor::detectEventType(const std::string& data) const {
    return detectEventTypeAt(data, data.find('{'));
}

ProtocolStreamExtractor::EventType ProtocolStreamExtractor::detectEventTypeAt(const std::string& data, size_t objStart) const {
    if (objStart == std::string::npos) {
        return EventType::Unknown;
    }
    std::vector<JsonMember> members;
    size_t closePos = 0;
    scanObjectMembers(data, objStart, members, closePos);
    for (const auto& m : members) {
        if (m.key == "createSurface") { return EventType::CreateSurface; }
        if (m.key == "updateComponents") { return EventType::UpdateComponents; }
        if (m.key == "updateDataModel") { return EventType::UpdateDataModel; }
        if (m.key == "appendDataModel") { return EventType::AppendDataModel; }
        if (m.key == "deleteSurface") { return EventType::DeleteSurface; }
    }
    return EventType::Unknown;
}

size_t ProtocolStreamExtractor::findSafeStringCut(const std::string& buffer, size_t fromPos, size_t endPos) {
    if (endPos > buffer.length()) {
        endPos = buffer.length();
    }
    size_t pos = fromPos;
    size_t safe = fromPos;
    while (pos < endPos) {
        unsigned char c = static_cast<unsigned char>(buffer[pos]);
        if (c == '"') {
            break; // End of the string content
        }
        if (c == '\\') {
            if (pos + 1 >= endPos) {
                break;
            }
            if (buffer[pos + 1] != 'u') {
                pos += 2;
            } else {
                if (pos + 6 > endPos) {
                    break;
                }
                int code = 0;
                for (size_t k = pos + 2; k < pos + 6; k++) {
                    int h = hexValue(buffer[k]);
                    code = (h < 0) ? -1 : ((code < 0) ? -1 : code * 16 + h);
                }
                pos += 6;
                if (code >= 0xD800 && code <= 0xDBFF) {
                    // A high surrogate is only valid together with the following \uDC00-\uDFFF
                    if (pos + 6 > endPos) {
                        break;
                    }
                    if (buffer[pos] == '\\' && buffer[pos + 1] == 'u') {
                        pos += 6;
                    }
                }
            }
        } else if (c < 0x80) {
            pos += 1;
        } else {
            size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
            if (pos + len > endPos) {
                break;
            }
            pos += len;
        }
        safe = pos;
    }
    return safe;
}

const char* ProtocolStreamExtractor::getEventTypeName(EventType type) const {
    if (type == EventType::CreateSurface) { return "createSurface"; }
    if (type == EventType::UpdateComponents) { return "updateComponents"; }
    if (type == EventType::UpdateDataModel) { return "updateDataModel"; }
    if (type == EventType::AppendDataModel) { return "appendDataModel"; }
    if (type == EventType::DeleteSurface) { return "deleteSurface"; }
    return "unknown";
}

// MARK: - DataModel streaming helpers

bool ProtocolStreamExtractor::isJsonArrayComplete(const std::string& json, size_t startPos, size_t& endPos) const {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = startPos; i < json.length(); i++) {
        char c = json[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == '"') {
            inString = !inString;
            continue;
        }

        if (!inString) {
            if (c == '[') {
                depth++;
            } else if (c == ']') {
                depth--;
                if (depth == 0) {
                    endPos = i;
                    return true;
                }
            }
        }
    }

    return false;
}

bool ProtocolStreamExtractor::findClosingQuote(const std::string& buffer, size_t startPos, size_t& outEndPos) const {
    bool escaped = false;
    for (size_t i = startPos; i < buffer.length(); i++) {
        char c = buffer[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            if (isValidClosingQuote(buffer, i)) {
                outEndPos = i;
                return true;
            }
        }
    }
    return false;
}

bool ProtocolStreamExtractor::isValidClosingQuote(const std::string& buffer, size_t quotePos) const {
    size_t pos = quotePos + 1;
    while (pos < buffer.length()) {
        char c = buffer[pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        pos++;
    }
    if (pos >= buffer.length()) {
        return true;
    }
    char nextChar = buffer[pos];
    return (nextChar == ',' || nextChar == ':' || nextChar == '}' || nextChar == ']' || nextChar == '\n' || nextChar == '\r');
}

bool ProtocolStreamExtractor::extractJsonStringAt(const std::string& buffer, size_t pos, std::string& outStr, size_t& outEndPos) const {
    if (pos >= buffer.length() || buffer[pos] != '"') {
        return false;
    }
    size_t closePos = 0;
    if (!findClosingQuote(buffer, pos + 1, closePos)) {
        return false;
    }
    outStr = buffer.substr(pos + 1, closePos - pos - 1);
    outEndPos = closePos;
    return true;
}

bool ProtocolStreamExtractor::extractCompleteJsonValue(const std::string& buffer, size_t startPos, size_t& outEndPos) const {
    if (startPos >= buffer.length()) {
        return false;
    }

    char c = buffer[startPos];

    if (c == '"') {
        return findClosingQuote(buffer, startPos + 1, outEndPos);
    }

    if (c == '{') {
        return isJsonObjectComplete(buffer, startPos, outEndPos);
    }

    if (c == '[') {
        return isJsonArrayComplete(buffer, startPos, outEndPos);
    }

    if (c == 't') {
        if (startPos + 3 < buffer.length() &&
            buffer[startPos + 1] == 'r' && buffer[startPos + 2] == 'u' && buffer[startPos + 3] == 'e') {
            outEndPos = startPos + 3;
            return true;
        }
        return false;
    }

    if (c == 'f') {
        if (startPos + 4 < buffer.length() &&
            buffer[startPos + 1] == 'a' && buffer[startPos + 2] == 'l' &&
            buffer[startPos + 3] == 's' && buffer[startPos + 4] == 'e') {
            outEndPos = startPos + 4;
            return true;
        }
        return false;
    }

    if (c == 'n') {
        if (startPos + 3 < buffer.length() &&
            buffer[startPos + 1] == 'u' && buffer[startPos + 2] == 'l' && buffer[startPos + 3] == 'l') {
            outEndPos = startPos + 3;
            return true;
        }
        return false;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        size_t pos = startPos + 1;
        while (pos < buffer.length()) {
            char ch = buffer[pos];
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') {
                pos++;
            } else {
                outEndPos = pos - 1;
                return true;
            }
        }
        return false;
    }

    return false;
}

// MARK: - DataModel streaming core methods
//
// A partial updateDataModel is turned into a sequence of smaller updateDataModel events whose
// in-order application yields exactly the data model the complete message would produce:
// every object/array that is not complete yet is first replaced by an empty container at its
// path, then filled member by member (complete members are sent whole). Streaming only starts
// once surfaceId, path and (if present) version are known, i.e. they precede "value"; otherwise
// the message is delivered whole when it is complete.

std::string ProtocolStreamExtractor::buildDataModelEventJson(const char* eventKey, const std::string& path,
                                                              const std::string& rawValue) const {
    std::string eventJson = "{";
    if (_dmHasVersion) {
        eventJson += "\"version\":" + quoteJsonString(_dmVersion) + ",";
    }
    eventJson += "\"";
    eventJson += eventKey;
    eventJson += "\":{\"surfaceId\":" + quoteJsonString(_dmSurfaceId);
    eventJson += ",\"path\":" + quoteJsonString(path);
    eventJson += ",\"value\":" + rawValue;
    eventJson += "}}";
    return eventJson;
}

void ProtocolStreamExtractor::emitDataModelUpdate(const std::string& path, const std::string& rawValue,
                                                  std::vector<ParseResult>& outResults) {
    if (!nlohmann::json::accept(rawValue)) {
        AGENUI_LOG("skipping invalid updateDataModel value at %s", path.c_str());
        return;
    }
    ParseResult result;
    result.type = ParseResult::Type::NormalEvent;
    result.eventType = EventType::UpdateDataModel;
    result.eventJson = buildDataModelEventJson("updateDataModel", path, rawValue);
    outResults.emplace_back(std::move(result));
}

void ProtocolStreamExtractor::resetDataModelStreamState() {
    _dmSurfaceId.clear();
    _dmBasePath.clear();
    _dmVersion.clear();
    _dmHasVersion = false;
    _dmValuePending = false;
    _dmParsePosition = 0;
    _dmValueStart = 0;
    _dmStack.clear();
    _dmMarkdownStreaming = false;
    _dmMarkdownFieldPath.clear();
    _dmMarkdownValueStart = 0;
    _dmMarkdownLastSentEnd = 0;
}

bool ProtocolStreamExtractor::startStreamingDataModel() {
    std::vector<JsonMember> members;
    size_t closePos = 0;
    scanObjectMembers(_dataBuffer, 0, members, closePos);

    std::string version;
    bool hasVersion = false;
    const JsonMember* payload = nullptr;
    for (const auto& m : members) {
        if (m.key == "version") {
            std::string raw;
            if (!rawStringValue(_dataBuffer, m, raw) || !decodeJsonString(raw, version)) {
                return false;
            }
            hasVersion = true;
        } else if (m.key == "updateDataModel") {
            payload = &m;
            break;
        }
    }
    if (!payload || payload->valueStart == std::string::npos || _dataBuffer[payload->valueStart] != '{') {
        return false;
    }
    if (!hasVersion) {
        return false; // A version may still follow the payload; deliver the message whole instead
    }

    std::vector<JsonMember> inner;
    scanObjectMembers(_dataBuffer, payload->valueStart, inner, closePos);
    std::string surfaceId;
    std::string path;
    bool havePath = false;
    for (const auto& m : inner) {
        std::string raw;
        if (m.key == "surfaceId") {
            if (!rawStringValue(_dataBuffer, m, raw) || !decodeJsonString(raw, surfaceId)) {
                return false;
            }
        } else if (m.key == "path") {
            if (!rawStringValue(_dataBuffer, m, raw) || !decodeJsonString(raw, path)) {
                return false;
            }
            havePath = true;
        } else if (m.key == "value") {
            if (surfaceId.empty() || !havePath || m.valueStart == std::string::npos) {
                return false;
            }
            resetDataModelStreamState();
            _dmSurfaceId = surfaceId;
            _dmBasePath = path.empty() ? "/" : path;
            _dmVersion = version;
            _dmHasVersion = hasVersion;
            _dmValueStart = m.valueStart;
            _dmParsePosition = m.valueStart;
            _dmValuePending = true;
            return true;
        }
    }
    return false;
}

void ProtocolStreamExtractor::processStreamingDataModel(std::vector<ParseResult>& outResults) {
    while (true) {
        // If a Markdown field is currently being streamed, handle its continuation first
        if (_dmMarkdownStreaming) {
            if (!continueMarkdownFieldStreaming(outResults)) {
                return; // Still streaming; wait for more data
            }
            // Field complete; continue to the next field
            continue;
        }

        if (_dmValuePending) {
            if (!consumeDataModelValue(_dmBasePath, _dmValueStart, outResults)) {
                return;
            }
            _dmValuePending = false;
            continue;
        }

        if (_dmStack.empty()) {
            finishDataModelMessage();
            return;
        }

        DmStackEntry::Type topType = _dmStack.back().type;
        std::string topPathPrefix = _dmStack.back().pathPrefix;

        if (topType == DmStackEntry::Type::Object) {
            // === Object mode: parse key-value pairs ===

            while (_dmParsePosition < _dataBuffer.length()) {
                char c = _dataBuffer[_dmParsePosition];
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != ',') {
                    break;
                }
                _dmParsePosition++;
            }

            if (_dmParsePosition >= _dataBuffer.length()) {
                return;
            }

            if (_dataBuffer[_dmParsePosition] == '}') {
                _dmParsePosition++;
                _dmStack.pop_back();
                continue;
            }

            if (_dataBuffer[_dmParsePosition] != '"') {
                abandonMessageIfComplete("unexpected token in updateDataModel value");
                return;
            }
            std::string rawKey;
            size_t keyEndPos = 0;
            if (!extractJsonStringAt(_dataBuffer, _dmParsePosition, rawKey, keyEndPos)) {
                return;
            }
            size_t pos = keyEndPos + 1;

            while (pos < _dataBuffer.length() && (
                _dataBuffer[pos] == ' ' || _dataBuffer[pos] == '\t' ||
                _dataBuffer[pos] == '\n' || _dataBuffer[pos] == '\r')) {
                pos++;
            }

            if (pos >= _dataBuffer.length()) {
                return;
            }
            if (_dataBuffer[pos] != ':') {
                abandonMessageIfComplete("missing ':' in updateDataModel value");
                return;
            }
            pos++;

            while (pos < _dataBuffer.length() && (
                _dataBuffer[pos] == ' ' || _dataBuffer[pos] == '\t' ||
                _dataBuffer[pos] == '\n' || _dataBuffer[pos] == '\r')) {
                pos++;
            }

            if (pos >= _dataBuffer.length()) {
                return;
            }

            // Keys are JSON-decoded, then escaped as JSON-pointer tokens (~0, ~1)
            std::string key;
            if (!decodeJsonString(rawKey, key)) {
                abandonMessageIfComplete("invalid key in updateDataModel value");
                return;
            }
            std::string fieldPath = appendPointerToken(topPathPrefix, key);

            if (!consumeDataModelValue(fieldPath, pos, outResults)) {
                return;
            }
            continue;

        } else {
            // === Array mode: recursively traverse elements (supports nested objects/arrays) ===

            while (_dmParsePosition < _dataBuffer.length()) {
                char c = _dataBuffer[_dmParsePosition];
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != ',') {
                    break;
                }
                _dmParsePosition++;
            }

            if (_dmParsePosition >= _dataBuffer.length()) {
                return;
            }

            if (_dataBuffer[_dmParsePosition] == ']') {
                _dmParsePosition++;
                _dmStack.pop_back();
                continue;
            }

            int currentIndex = _dmStack.back().arrayIndex;
            std::string elementPath = appendPointerToken(topPathPrefix, std::to_string(currentIndex));

            // consumeDataModelValue may push a child entry, so count the element first
            _dmStack.back().arrayIndex++;
            if (!consumeDataModelValue(elementPath, _dmParsePosition, outResults)) {
                _dmStack.back().arrayIndex--;
                return;
            }
            continue;
        }
    }
}

bool ProtocolStreamExtractor::consumeDataModelValue(const std::string& path, size_t pos,
                                                    std::vector<ParseResult>& outResults) {
    if (pos >= _dataBuffer.length()) {
        return false;
    }
    char c = _dataBuffer[pos];

    size_t valueEndPos = 0;
    if (extractCompleteJsonValue(_dataBuffer, pos, valueEndPos)) {
        emitDataModelUpdate(path, _dataBuffer.substr(pos, valueEndPos - pos + 1), outResults);
        _dmParsePosition = valueEndPos + 1;
        return true;
    }

    if (c == '{' || c == '[') {
        // Replace whatever is at this path with an empty container, then fill it member by member
        emitDataModelUpdate(path, (c == '{') ? "{}" : "[]", outResults);
        _dmStack.push_back(DmStackEntry(
            (c == '{') ? DmStackEntry::Type::Object : DmStackEntry::Type::Array, path, 0));
        _dmParsePosition = pos + 1;
        return true;
    }

    // For string values, check whether this is a streaming binding path
    if (c == '"' && _plugin && _plugin->shouldStreamField(path)) {
        startMarkdownFieldStreaming(path, pos, outResults);
        return true;
    }

    return false; // Wait until the scalar value is complete
}

void ProtocolStreamExtractor::finishDataModelMessage() {
    // After the value we are inside updateDataModel (depth 2); skip any members that follow
    size_t endPos = 0;
    if (!findObjectEnd(_dataBuffer, _dmParsePosition, 2, endPos)) {
        return; // Wait for the rest of the message
    }
    consumeMessage(endPos);
    resetDataModelStreamState();
    _streamState = StreamState::Idle;
}

// MARK: - Field-level incremental streaming (sub-process embedded in DM streaming)

void ProtocolStreamExtractor::emitAppendDataModel(const std::string& path, const std::string& delta,
                                                  std::vector<ParseResult>& outResults) {
    std::string rawValue = "\"" + delta + "\"";
    if (!nlohmann::json::accept(rawValue)) {
        AGENUI_LOG("skipping invalid appendDataModel delta at %s", path.c_str());
        return;
    }
    ParseResult result;
    result.type = ParseResult::Type::NormalEvent;
    result.eventType = EventType::AppendDataModel;
    result.eventJson = buildDataModelEventJson("appendDataModel", path, rawValue);
    outResults.emplace_back(std::move(result));
}

void ProtocolStreamExtractor::startMarkdownFieldStreaming(const std::string& fieldPath, size_t valueQuotePos,
                                                           std::vector<ParseResult>& outResults) {
    size_t contentStart = valueQuotePos + 1; // After the opening "

    size_t closePos = 0;
    if (findClosingQuote(_dataBuffer, contentStart, closePos)) {
        // String is complete; send it as a regular field (complete JSON string value with quotes)
        std::string rawValue = _dataBuffer.substr(valueQuotePos, closePos - valueQuotePos + 1);
        emitDataModelUpdate(fieldPath, rawValue, outResults);
        _dmParsePosition = closePos + 1;
        // _dmMarkdownStreaming remains false
        return;
    }

    // String is incomplete; enter field-level incremental streaming. The first event sets the
    // field (possibly to ""), so every later appendDataModel has a string to append to.
    _dmMarkdownStreaming = true;
    _dmMarkdownFieldPath = fieldPath;
    _dmMarkdownValueStart = contentStart;

    size_t safeEnd = findSafeStringCut(_dataBuffer, contentStart, _dataBuffer.length());
    std::string partialContent = _dataBuffer.substr(contentStart, safeEnd - contentStart);
    emitDataModelUpdate(fieldPath, "\"" + partialContent + "\"", outResults);
    _dmMarkdownLastSentEnd = safeEnd;
}

bool ProtocolStreamExtractor::continueMarkdownFieldStreaming(std::vector<ParseResult>& outResults) {
    // _dmMarkdownLastSentEnd is always a character boundary, so scanning can resume there
    size_t closePos = 0;
    if (findClosingQuote(_dataBuffer, _dmMarkdownLastSentEnd, closePos)) {
        // String is complete
        if (closePos > _dmMarkdownLastSentEnd) {
            std::string delta = _dataBuffer.substr(_dmMarkdownLastSentEnd, closePos - _dmMarkdownLastSentEnd);
            emitAppendDataModel(_dmMarkdownFieldPath, delta, outResults);
        }

        _dmParsePosition = closePos + 1;
        _dmMarkdownStreaming = false;
        return true; // Field complete
    }

    // String is still incomplete; send what can be cut safely
    size_t safeEnd = findSafeStringCut(_dataBuffer, _dmMarkdownLastSentEnd, _dataBuffer.length());
    if (safeEnd > _dmMarkdownLastSentEnd) {
        std::string delta = _dataBuffer.substr(_dmMarkdownLastSentEnd, safeEnd - _dmMarkdownLastSentEnd);
        emitAppendDataModel(_dmMarkdownFieldPath, delta, outResults);
        _dmMarkdownLastSentEnd = safeEnd;
    }

    return false; // Still streaming
}

} // namespace A2ui
