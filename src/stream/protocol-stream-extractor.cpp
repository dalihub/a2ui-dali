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

// Keys after which scanObjectMembers() stops without scanning the (possibly huge) value
const char* const kMessageKeys[] = {
    "createSurface", "updateComponents", "updateDataModel", "appendDataModel", "deleteSurface", nullptr};
const char* const kUpdateComponentsKey[] = {"updateComponents", nullptr};
const char* const kComponentsKey[] = {"components", nullptr};
const char* const kUpdateDataModelKey[] = {"updateDataModel", nullptr};
const char* const kValueKey[] = {"value", nullptr};

// Whether `buffer` from `pos` (up to its end) can still become a valid scalar
bool isScalarPrefix(const std::string& buffer, size_t pos) {
    char c = buffer[pos];
    if (c == '-' || (c >= '0' && c <= '9')) {
        return true;
    }
    const char* literal = (c == 't') ? "true" : (c == 'f') ? "false" : (c == 'n') ? "null" : nullptr;
    if (!literal) {
        return false;
    }
    for (size_t k = 0; literal[k] && pos + k < buffer.length(); k++) {
        if (buffer[pos + k] != literal[k]) {
            return false;
        }
    }
    return true;
}

// Containers nested deeper than this are not split member by member but sent whole once
// complete (like a container holding a null). Every per-member event repeats the full path of its
// container, so without a cap a pathologically deep value (e.g. 100k levels in 1 MB) produces
// output quadratic in its depth (27 GB for 1 MB). A2UI data models are a few levels deep (the
// deepest value in the repo's sample and e2e streams has 5 levels), so 32 leaves ample room and
// bounds the per-member output to a small multiple of the input.
constexpr size_t kMaxStreamedDepth = 32;

// Keys that cannot be addressed member by member with the renderer's JSON-pointer writes:
// "" (would address the parent), '/' and '~' (not decoded by the renderer), all-digit keys
// (a fresh renderer model turns them into array indices), keys with leading or trailing
// whitespace (the renderer trims path segments), and keys written with escapes
// (the renderer re-embeds path segments unescaped, and decodes \u surrogate pairs differently
// inside a value than the re-encoded key would read).
bool needsWholeContainer(const std::string& rawKey, const std::string& key) {
    return key.empty() || rawKey.find('\\') != std::string::npos ||
           key.find_first_of("/~") != std::string::npos ||
           key.find_first_not_of("0123456789") == std::string::npos ||
           isJsonWhitespace(key.front()) || isJsonWhitespace(key.back());
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
            resetMessageCache();
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
    resetMessageCache();
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

        // Both are cached per message, so a message arriving in many chunks is scanned once
        size_t endPos = 0;
        bool complete = currentMessageEnd(endPos);
        if (_msgType == EventType::Unknown) {
            if (_msgMembers.objStart != 0) {
                startMemberScan(_msgMembers, 0);
            }
            advanceMemberScan(_dataBuffer, _msgMembers, kMessageKeys);
            _msgType = eventTypeOfMembers(_msgMembers.members);
        }
        EventType eventType = _msgType;

        if (eventType == EventType::UpdateComponents) {
            // Complete or not, components are delivered one by one from the message in place
            if ((!complete && _msgWaitWhole) || !startStreamingComponents(complete)) {
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
            if (eventType == EventType::UpdateDataModel && !_msgWaitWhole && startStreamingDataModel()) {
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
    // A complete message is scanned once in full (the version may follow the payload); a partial
    // one uses the resumable scans, which stop where the payload value starts
    std::vector<JsonMember> fullMembers;
    size_t closePos = 0;
    if (messageComplete) {
        scanObjectMembers(_dataBuffer, 0, fullMembers, closePos);
    } else {
        if (_msgMembers.objStart != 0) {
            startMemberScan(_msgMembers, 0);
        }
        advanceMemberScan(_dataBuffer, _msgMembers, kMessageKeys);
    }
    const std::vector<JsonMember>& members = messageComplete ? fullMembers : _msgMembers.members;

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
        _msgWaitWhole = true;
        return false; // A version may still follow the payload; wait for the whole message
    }

    std::vector<JsonMember> fullInner;
    if (messageComplete) {
        scanObjectMembers(_dataBuffer, payload->valueStart, fullInner, closePos);
    } else {
        if (_payloadMembers.objStart != payload->valueStart) {
            startMemberScan(_payloadMembers, payload->valueStart);
        }
        advanceMemberScan(_dataBuffer, _payloadMembers, kComponentsKey);
    }
    const std::vector<JsonMember>& inner = messageComplete ? fullInner : _payloadMembers.members;
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
                if (!haveSurfaceId) {
                    _msgWaitWhole = true; // surfaceId must precede the array to stream it
                }
                break;
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
    // Other members may follow the components array before the two closing braces; the
    // message ends where its outermost object closes.
    size_t endPos = 0;
    if (!currentMessageEnd(endPos)) {
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
        resetMessageCache();
    }
}

void ProtocolStreamExtractor::consumeMessage(size_t endPos) {
    _dataBuffer.erase(0, endPos + 1);
    _parsePosition = 0;
    resetMessageCache();
}

void ProtocolStreamExtractor::resetMessageCache() {
    if (_plugin) {
        _plugin->onBufferConsumed();
    }
    _msgScan = ScanState();
    _msgComplete = false;
    _msgEnd = 0;
    _msgType = EventType::Unknown;
    _msgWaitWhole = false;
    _msgMembers = MemberScan();
    _payloadMembers = MemberScan();
    _componentScanStart = std::string::npos;
}

bool ProtocolStreamExtractor::currentMessageEnd(size_t& outEndPos) {
    // Same rule as isJsonObjectComplete(_dataBuffer, 0, ...), resumed where the last call stopped
    if (!_msgComplete) {
        _msgComplete = advanceScan(_dataBuffer, _msgScan, '{', '}', _msgEnd);
    }
    outEndPos = _msgEnd;
    return _msgComplete;
}

void ProtocolStreamExtractor::abandonMessageIfComplete(const char* reason) {
    size_t endPos = 0;
    if (!currentMessageEnd(endPos)) {
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

    // Resume the scan of this component where the previous chunk stopped
    if (_componentScanStart != _parsePosition) {
        _componentScanStart = _parsePosition;
        _componentScan = ScanState();
        _componentScan.pos = _parsePosition;
    }
    size_t endPos = 0;
    if (advanceScan(_dataBuffer, _componentScan, '{', '}', endPos)) {
        outComponent = _dataBuffer.substr(_parsePosition, endPos - _parsePosition + 1);
        _parsePosition = endPos + 1;
        _componentScanStart = std::string::npos;
        return true;
    }

    // JSON object is incomplete; wait for more data
    return false;
}

bool ProtocolStreamExtractor::advanceScan(const std::string& buffer, ScanState& state, char open, char close,
                                          size_t& outEndPos) {
    // Same rules as isJsonObjectComplete() / isJsonArrayComplete()
    for (; state.pos < buffer.length(); state.pos++) {
        char c = buffer[state.pos];

        if (state.escaped) {
            state.escaped = false;
            continue;
        }
        if (c == '\\') {
            state.escaped = true;
            continue;
        }
        if (c == '"') {
            state.inString = !state.inString;
            continue;
        }
        if (!state.inString) {
            if (c == open) {
                state.depth++;
            } else if (c == close) {
                state.depth--;
                if (state.depth == 0) {
                    outEndPos = state.pos;
                    state.pos++;
                    return true;
                }
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
                                                std::vector<JsonMember>& outMembers, size_t& outClosePos,
                                                const char* const* stopKeys) const {
    MemberScan scan;
    startMemberScan(scan, objStart);
    advanceMemberScan(buffer, scan, stopKeys);
    outMembers = scan.members;
    if (scan.phase == MemberScan::Phase::Closed) {
        outClosePos = scan.closePos;
        return true;
    }
    return false;
}

void ProtocolStreamExtractor::startMemberScan(MemberScan& scan, size_t objStart) {
    scan = MemberScan();
    scan.objStart = objStart;
    scan.pos = (objStart == std::string::npos) ? 0 : objStart + 1;
}

void ProtocolStreamExtractor::advanceMemberScan(const std::string& buffer, MemberScan& scan,
                                                const char* const* stopKeys) const {
    using Phase = MemberScan::Phase;
    if (scan.objStart >= buffer.length() || buffer[scan.objStart] != '{') {
        return;
    }
    const size_t length = buffer.length();
    while (true) {
        switch (scan.phase) {
            case Phase::BeforeKey: {
                while (scan.pos < length && (isJsonWhitespace(buffer[scan.pos]) || buffer[scan.pos] == ',')) {
                    scan.pos++;
                }
                if (scan.pos >= length) {
                    return;
                }
                if (buffer[scan.pos] == '}') {
                    scan.closePos = scan.pos;
                    scan.phase = Phase::Closed;
                    return;
                }
                if (buffer[scan.pos] != '"') {
                    scan.phase = Phase::Error;
                    return;
                }
                scan.tokenStart = scan.pos++;
                scan.escaped = false;
                scan.phase = Phase::InKey;
                break;
            }
            case Phase::InKey: {
                for (; scan.pos < length; scan.pos++) {
                    char c = buffer[scan.pos];
                    if (scan.escaped) {
                        scan.escaped = false;
                    } else if (c == '\\') {
                        scan.escaped = true;
                    } else if (c == '"') {
                        break;
                    }
                }
                if (scan.pos >= length) {
                    return;
                }
                JsonMember member;
                member.key = buffer.substr(scan.tokenStart + 1, scan.pos - scan.tokenStart - 1);
                scan.members.push_back(member);
                scan.pos++;
                scan.phase = Phase::Colon;
                break;
            }
            case Phase::Colon: {
                while (scan.pos < length && isJsonWhitespace(buffer[scan.pos])) {
                    scan.pos++;
                }
                if (scan.pos >= length) {
                    return;
                }
                if (buffer[scan.pos] != ':') {
                    scan.phase = Phase::Error;
                    return;
                }
                scan.pos++;
                scan.phase = Phase::BeforeValue;
                break;
            }
            case Phase::BeforeValue: {
                while (scan.pos < length && isJsonWhitespace(buffer[scan.pos])) {
                    scan.pos++;
                }
                if (scan.pos >= length) {
                    return;
                }
                JsonMember& member = scan.members.back();
                member.valueStart = scan.pos;
                for (const char* const* k = stopKeys; k && *k; k++) {
                    if (member.key == *k) {
                        scan.phase = Phase::Stopped; // The caller only needs to know where it starts
                        return;
                    }
                }
                char c = buffer[scan.pos];
                scan.valueKind = (c == '"' || c == '{' || c == '[') ? c : 's';
                if (c == '"') {
                    scan.pos++;
                    scan.escaped = false;
                } else if (c == '{' || c == '[') {
                    scan.valueScan = ScanState();
                    scan.valueScan.pos = scan.pos;
                }
                scan.phase = Phase::InValue;
                break;
            }
            case Phase::InValue: {
                JsonMember& member = scan.members.back();
                size_t valueEnd = 0;
                bool done = false;
                if (scan.valueKind == '"') {
                    for (; scan.pos < length; scan.pos++) {
                        char c = buffer[scan.pos];
                        if (scan.escaped) {
                            scan.escaped = false;
                        } else if (c == '\\') {
                            scan.escaped = true;
                        } else if (c == '"') {
                            valueEnd = scan.pos;
                            done = true;
                            break;
                        }
                    }
                } else if (scan.valueKind == 's') {
                    done = extractCompleteJsonValue(buffer, member.valueStart, valueEnd);
                } else {
                    done = advanceScan(buffer, scan.valueScan, scan.valueKind,
                                       (scan.valueKind == '{') ? '}' : ']', valueEnd);
                }
                if (!done) {
                    return;
                }
                member.valueEnd = valueEnd;
                member.complete = true;
                scan.pos = valueEnd + 1;
                scan.phase = Phase::BeforeKey;
                break;
            }
            case Phase::Stopped:
            case Phase::Closed:
            case Phase::Error:
                return;
        }
    }
}

ProtocolStreamExtractor::EventType ProtocolStreamExtractor::eventTypeOfMembers(const std::vector<JsonMember>& members) {
    for (const auto& m : members) {
        if (m.key == "createSurface") { return EventType::CreateSurface; }
        if (m.key == "updateComponents") { return EventType::UpdateComponents; }
        if (m.key == "updateDataModel") { return EventType::UpdateDataModel; }
        if (m.key == "appendDataModel") { return EventType::AppendDataModel; }
        if (m.key == "deleteSurface") { return EventType::DeleteSurface; }
    }
    return EventType::Unknown;
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
    scanObjectMembers(data, objStart, members, closePos, kMessageKeys);
    return eventTypeOfMembers(members);
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
// once surfaceId, path and version are known, i.e. they precede "value"; otherwise the message
// is delivered whole when it is complete.
//
// The renderer deletes on "value":null and writes keys through unescaped JSON-pointer segments,
// so a container with a null member or an unusual key (see needsWholeContainer) is not split:
// it is sent as one event at its own path once complete. A malformed message is delivered
// whole, as received, exactly as an unsplit stream would deliver it.

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

bool ProtocolStreamExtractor::emitDataModelUpdate(const std::string& path, const std::string& rawValue,
                                                  std::vector<ParseResult>& outResults) {
    if (!nlohmann::json::accept(rawValue)) {
        enterInvalidDataModel("invalid value");
        return false;
    }
    ParseResult result;
    result.type = ParseResult::Type::NormalEvent;
    result.eventType = EventType::UpdateDataModel;
    result.eventJson = buildDataModelEventJson("updateDataModel", path, rawValue);
    outResults.emplace_back(std::move(result));
    return true;
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
    _dmWholePending = false;
    _dmWholePath.clear();
    _dmWholeStart = 0;
    _dmInvalid = false;
    _dmKeyScan = StringScan();
    _dmStringScan = StringScan();
    _dmMarkdownStreaming = false;
    _dmMarkdownFieldPath.clear();
    _dmMarkdownValueStart = 0;
    _dmMarkdownLastSentEnd = 0;
}

bool ProtocolStreamExtractor::startStreamingDataModel() {
    // Resumable scans: each call only looks at bytes that arrived since the previous one
    if (_msgMembers.objStart != 0) {
        startMemberScan(_msgMembers, 0);
    }
    advanceMemberScan(_dataBuffer, _msgMembers, kMessageKeys);
    const std::vector<JsonMember>& members = _msgMembers.members;

    const JsonMember* versionMember = nullptr;
    const JsonMember* payload = nullptr;
    for (const auto& m : members) {
        if (m.key == "version") {
            versionMember = &m;
        } else if (m.key == "updateDataModel") {
            payload = &m;
            break;
        }
    }
    if (!payload || payload->valueStart == std::string::npos) {
        return false;
    }
    if (_dataBuffer[payload->valueStart] != '{' || !versionMember) {
        // A version may still follow the payload; deliver the message whole instead
        _msgWaitWhole = true;
        return false;
    }

    if (_payloadMembers.objStart != payload->valueStart) {
        startMemberScan(_payloadMembers, payload->valueStart);
    }
    advanceMemberScan(_dataBuffer, _payloadMembers, kValueKey);
    if (_payloadMembers.phase != MemberScan::Phase::Stopped) {
        return false; // "value" has not started yet (decode the header only once it has)
    }
    const std::vector<JsonMember>& inner = _payloadMembers.members;

    std::string version;
    std::string rawVersion;
    if (!rawStringValue(_dataBuffer, *versionMember, rawVersion) || !decodeJsonString(rawVersion, version)) {
        _msgWaitWhole = true;
        return false;
    }
    bool hasVersion = true;
    std::string surfaceId;
    std::string path;
    bool havePath = false;
    for (const auto& m : inner) {
        std::string raw;
        if (m.key == "surfaceId" || m.key == "path") {
            if (!m.complete) {
                return false;
            }
            std::string& target = (m.key == "path") ? path : surfaceId;
            if (!rawStringValue(_dataBuffer, m, raw) || !decodeJsonString(raw, target)) {
                _msgWaitWhole = true;
                return false;
            }
            havePath = havePath || m.key == "path";
        } else if (m.key == "value") {
            if (surfaceId.empty() || !havePath) {
                _msgWaitWhole = true; // surfaceId and path must precede the value
                return false;
            }
            if (m.valueStart == std::string::npos) {
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
        if (_dmInvalid) {
            // Deliver the message exactly as an unsplit stream would: whole, as received
            size_t endPos = 0;
            if (!currentMessageEnd(endPos)) {
                return;
            }
            ParseResult result;
            result.type = ParseResult::Type::NormalEvent;
            result.eventType = EventType::UpdateDataModel;
            result.eventJson = _dataBuffer.substr(0, endPos + 1);
            outResults.emplace_back(std::move(result));
            consumeMessage(endPos);
            resetDataModelStreamState();
            _streamState = StreamState::Idle;
            return;
        }

        if (_dmWholePending) {
            size_t endPos = 0;
            char open = _dataBuffer[_dmWholeStart];
            if (!advanceScan(_dataBuffer, _dmWholeScan, open, (open == '{') ? '}' : ']', endPos)) {
                return;
            }
            _dmWholePending = false;
            if (emitDataModelUpdate(_dmWholePath, _dataBuffer.substr(_dmWholeStart, endPos - _dmWholeStart + 1),
                                    outResults)) {
                _dmParsePosition = endPos + 1;
            }
            continue;
        }

        // If a Markdown field is currently being streamed, handle its continuation first
        if (_dmMarkdownStreaming) {
            if (!continueMarkdownFieldStreaming(outResults)) {
                return; // Still streaming; wait for more data
            }
            // Field complete; continue to the next field
            continue;
        }

        if (_dmValuePending) {
            if (!consumeDataModelValue(_dmBasePath, _dmValueStart, false, outResults)) {
                return;
            }
            _dmValuePending = false;
            continue;
        }

        if (_dmStack.empty()) {
            finishDataModelMessage(outResults);
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
                enterInvalidDataModel("unexpected token in updateDataModel value");
                continue;
            }
            size_t keyEndPos = 0;
            if (!scanStringValueEnd(_dmKeyScan, _dmParsePosition, keyEndPos)) {
                return;
            }
            std::string rawKey = _dataBuffer.substr(_dmParsePosition + 1, keyEndPos - _dmParsePosition - 1);
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
                enterInvalidDataModel("missing ':' in updateDataModel value");
                continue;
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
                enterInvalidDataModel("invalid key in updateDataModel value");
                continue;
            }
            if (needsWholeContainer(rawKey, key)) {
                startWholeContainer();
                continue;
            }
            std::string fieldPath = appendPointerToken(topPathPrefix, key);

            if (!consumeDataModelValue(fieldPath, pos, true, outResults)) {
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
            if (!consumeDataModelValue(elementPath, _dmParsePosition, true, outResults)) {
                _dmStack.back().arrayIndex--;
                return;
            }
            continue;
        }
    }
}

bool ProtocolStreamExtractor::consumeDataModelValue(const std::string& path, size_t pos, bool isMember,
                                                    std::vector<ParseResult>& outResults) {
    if (pos >= _dataBuffer.length()) {
        return false;
    }
    char c = _dataBuffer[pos];

    if (isMember && c == 'n') {
        // The renderer deletes on "value":null, so a null member cannot be sent on its own
        // (an array would also get a gap); send the enclosing container whole instead.
        startWholeContainer();
        return true;
    }

    if (c == '"') {
        // For string values, check whether this is a streaming binding path
        if (_plugin && _plugin->shouldStreamField(path)) {
            startMarkdownFieldStreaming(path, pos, outResults);
            return true;
        }
        size_t closePos = 0;
        if (!scanStringValueEnd(_dmStringScan, pos, closePos)) {
            return false;
        }
        if (emitDataModelUpdate(path, _dataBuffer.substr(pos, closePos - pos + 1), outResults)) {
            _dmParsePosition = closePos + 1;
        }
        return true;
    }

    if ((c == '{' || c == '[') && _dmStack.size() >= kMaxStreamedDepth) {
        // Too deep to split: send this container whole at its path once it is complete
        _dmWholePending = true;
        _dmWholePath = path;
        _dmWholeStart = pos;
        _dmWholeScan = ScanState();
        _dmWholeScan.pos = pos;
        return true;
    }

    size_t valueEndPos = 0;
    if (extractCompleteJsonValue(_dataBuffer, pos, valueEndPos)) {
        if (emitDataModelUpdate(path, _dataBuffer.substr(pos, valueEndPos - pos + 1), outResults)) {
            _dmParsePosition = valueEndPos + 1;
        }
        return true;
    }

    if (c == '{' || c == '[') {
        // Replace whatever is at this path with an empty container, then fill it member by member
        if (emitDataModelUpdate(path, (c == '{') ? "{}" : "[]", outResults)) {
            _dmStack.push_back(DmStackEntry(
                (c == '{') ? DmStackEntry::Type::Object : DmStackEntry::Type::Array, path, 0, pos));
            _dmParsePosition = pos + 1;
        }
        return true;
    }

    if (!isScalarPrefix(_dataBuffer, pos)) {
        enterInvalidDataModel("invalid value");
        return true;
    }
    return false; // Wait until the scalar value is complete
}

bool ProtocolStreamExtractor::scanStringValueEnd(StringScan& scan, size_t quotePos, size_t& outClosePos) {
    // Resumable search for the closing quote of the string that opens at quotePos
    if (scan.start != quotePos) {
        scan.start = quotePos;
        scan.pos = quotePos + 1;
        scan.escaped = false;
    }
    for (; scan.pos < _dataBuffer.length(); scan.pos++) {
        char c = _dataBuffer[scan.pos];
        if (scan.escaped) {
            scan.escaped = false;
        } else if (c == '\\') {
            scan.escaped = true;
        } else if (c == '"') {
            outClosePos = scan.pos; // Kept, so asking again for this string is O(1)
            return true;
        }
    }
    return false;
}

void ProtocolStreamExtractor::startWholeContainer() {
    // Stop member-by-member delivery for the innermost open container; it is sent as one
    // updateDataModel at its own path once it is complete.
    const DmStackEntry& top = _dmStack.back();
    _dmWholePending = true;
    _dmWholePath = top.pathPrefix;
    _dmWholeStart = top.start;
    _dmWholeScan = ScanState();
    _dmWholeScan.pos = top.start;
    _dmStack.pop_back();
}

void ProtocolStreamExtractor::enterInvalidDataModel(const char* reason) {
    AGENUI_LOG("updateDataModel: %s; delivering the message unsplit", reason);
    _dmInvalid = true;
    _dmValuePending = false;
    _dmWholePending = false;
    _dmMarkdownStreaming = false;
    _dmStack.clear();
}

void ProtocolStreamExtractor::finishDataModelMessage(std::vector<ParseResult>& outResults) {
    // Other members may follow the value; the message ends where its outermost object closes
    size_t endPos = 0;
    if (!currentMessageEnd(endPos)) {
        return; // Wait for the rest of the message
    }
    if (!nlohmann::json::accept(_dataBuffer.data(), _dataBuffer.data() + endPos + 1)) {
        // Malformed outside the parts streamed so far (e.g. a missing comma): an unsplit stream
        // delivers such a message as received, so do the same. The member events already sent
        // for it cannot be taken back.
        AGENUI_LOG("updateDataModel: malformed message; delivering it unsplit");
        ParseResult result;
        result.type = ParseResult::Type::NormalEvent;
        result.eventType = EventType::UpdateDataModel;
        result.eventJson = _dataBuffer.substr(0, endPos + 1);
        outResults.emplace_back(std::move(result));
    }
    consumeMessage(endPos);
    resetDataModelStreamState();
    _streamState = StreamState::Idle;
}

// MARK: - Field-level incremental streaming (sub-process embedded in DM streaming)

bool ProtocolStreamExtractor::emitAppendDataModel(const std::string& path, const std::string& delta,
                                                  std::vector<ParseResult>& outResults) {
    std::string rawValue = "\"" + delta + "\"";
    if (!nlohmann::json::accept(rawValue)) {
        enterInvalidDataModel("invalid string content");
        return false;
    }
    ParseResult result;
    result.type = ParseResult::Type::NormalEvent;
    result.eventType = EventType::AppendDataModel;
    result.eventJson = buildDataModelEventJson("appendDataModel", path, rawValue);
    outResults.emplace_back(std::move(result));
    return true;
}

void ProtocolStreamExtractor::startMarkdownFieldStreaming(const std::string& fieldPath, size_t valueQuotePos,
                                                           std::vector<ParseResult>& outResults) {
    size_t contentStart = valueQuotePos + 1; // After the opening "

    size_t closePos = 0;
    if (findClosingQuote(_dataBuffer, contentStart, closePos)) {
        // String is complete; send it as a regular field (complete JSON string value with quotes)
        std::string rawValue = _dataBuffer.substr(valueQuotePos, closePos - valueQuotePos + 1);
        if (emitDataModelUpdate(fieldPath, rawValue, outResults)) {
            _dmParsePosition = closePos + 1;
        }
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
    if (!emitDataModelUpdate(fieldPath, "\"" + partialContent + "\"", outResults)) {
        return; // Invalid content; the message is delivered whole instead
    }
    _dmMarkdownLastSentEnd = safeEnd;
}

bool ProtocolStreamExtractor::continueMarkdownFieldStreaming(std::vector<ParseResult>& outResults) {
    // _dmMarkdownLastSentEnd is always a character boundary, so scanning can resume there
    size_t closePos = 0;
    if (findClosingQuote(_dataBuffer, _dmMarkdownLastSentEnd, closePos)) {
        // String is complete
        if (closePos > _dmMarkdownLastSentEnd) {
            std::string delta = _dataBuffer.substr(_dmMarkdownLastSentEnd, closePos - _dmMarkdownLastSentEnd);
            if (!emitAppendDataModel(_dmMarkdownFieldPath, delta, outResults)) {
                return true; // Invalid content; handled by the invalid-message path
            }
        }

        _dmParsePosition = closePos + 1;
        _dmMarkdownStreaming = false;
        return true; // Field complete
    }

    // String is still incomplete; send what can be cut safely
    size_t safeEnd = findSafeStringCut(_dataBuffer, _dmMarkdownLastSentEnd, _dataBuffer.length());
    if (safeEnd > _dmMarkdownLastSentEnd) {
        std::string delta = _dataBuffer.substr(_dmMarkdownLastSentEnd, safeEnd - _dmMarkdownLastSentEnd);
        if (!emitAppendDataModel(_dmMarkdownFieldPath, delta, outResults)) {
            return true; // Invalid content; handled by the invalid-message path
        }
        _dmMarkdownLastSentEnd = safeEnd;
    }

    return false; // Still streaming
}

} // namespace A2ui
