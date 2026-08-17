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

// Streaming parser example.
//
//   ./a2ui-streaming-parser [path/to/messages.json]
//
// Reads an A2UI v0.9 JSON file (default: ../samples/login-form.json) and feeds
// it in fixed-size chunks through StreamingContentParser, which extracts each
// complete JSON object and dispatches it to an IStreamEventSink implementation
// that prints the jsonData to stdout.
//
// In addition to printing, the sink *verifies* each parsed event: it
// re-deserializes the jsonData with nlohmann/json, checks that it is valid
// JSON with the expected top-level key (e.g. "createSurface"), and asserts
// key fields (version, surfaceId, components array, data model value).
// After the stream ends, a summary of PASS/FAIL checks is printed.

//
// StreamingContentParser expects a stream of *individual* A2UI event objects
// (JSONL-style), not a top-level JSON array.  The sample files under
// examples/samples/ are JSON arrays, so this example flattens the array into a
// concatenation of its element objects before feeding it to the parser —
// exactly how an A2UI transport would deliver the events.
//
// Pipeline:
//   StreamingContentParser -> ProtocolStreamExtractor -> IStreamEventSink

#include "stream/streaming-content-parser.h"
#include "stream/stream-event-sink.h"
#include "stream/error-code.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// Size of each chunk fed to the parser to simulate streaming arrival.
constexpr std::size_t kChunkSize = 64;

// --- AI-generated section starts --[20260817-212800-]-
// --- Code Writer: Cline SR
// --- Designer: (none)
//
// Record of one verification check performed inside a sink callback.
struct CheckResult
{
    std::string eventName;   // e.g. "createSurface"
    std::string description; // human-readable check description
    bool passed = false;
};

// IStreamEventSink implementation that prints the jsonData received for each
// parsed A2UI event *and* verifies its content by re-deserializing the JSON
// and asserting key fields.  Collected checks are summarized after the stream.
class VerifyingSink : public A2ui::IStreamEventSink
{
public:
    /// Returns all check results collected so far.
    const std::vector<CheckResult>& checks() const { return _checks; }

    /// Total number of events received.
    int eventCount() const { return _eventCount; }

    A2ui::AGenUIExeCode createSurface(const std::string& jsonData) override
    {
        ++_eventCount;
        std::cout << "=== createSurface ===" << std::endl;
        std::cout << jsonData << std::endl;

        nlohmann::json j = parseOrFail(jsonData, "createSurface");
        if(j.is_null()) return A2ui::Execute_Success;

        // Must contain the "createSurface" object with a non-empty surfaceId.
        checkField(j.contains("createSurface"), "createSurface", "has 'createSurface' key");
        if(j.contains("createSurface") && j["createSurface"].is_object())
        {
            const auto& cs = j["createSurface"];
            bool hasSurfaceId = cs.contains("surfaceId") &&
                                cs["surfaceId"].is_string() &&
                                !cs["surfaceId"].get<std::string>().empty();
            checkField(hasSurfaceId, "createSurface", "surfaceId is non-empty");
        }
        checkVersion(j, "createSurface");
        return A2ui::Execute_Success;
    }

    A2ui::AGenUIExeCode deleteSurface(const std::string& jsonData) override
    {
        ++_eventCount;
        std::cout << "=== deleteSurface ===" << std::endl;
        std::cout << jsonData << std::endl;

        nlohmann::json j = parseOrFail(jsonData, "deleteSurface");
        if(j.is_null()) return A2ui::Execute_Success;

        checkField(j.contains("deleteSurface"), "deleteSurface", "has 'deleteSurface' key");
        if(j.contains("deleteSurface") && j["deleteSurface"].is_object())
        {
            const auto& ds = j["deleteSurface"];
            bool hasSurfaceId = ds.contains("surfaceId") &&
                                ds["surfaceId"].is_string() &&
                                !ds["surfaceId"].get<std::string>().empty();
            checkField(hasSurfaceId, "deleteSurface", "surfaceId is non-empty");
        }
        return A2ui::Execute_Success;
    }

    A2ui::AGenUIExeCode updateComponents(const std::string& jsonData) override
    {
        ++_eventCount;
        std::cout << "=== updateComponents ===" << std::endl;
        std::cout << jsonData << std::endl;

        nlohmann::json j = parseOrFail(jsonData, "updateComponents");
        if(j.is_null()) return A2ui::Execute_Success;

        checkField(j.contains("updateComponents"), "updateComponents", "has 'updateComponents' key");
        if(j.contains("updateComponents") && j["updateComponents"].is_object())
        {
            const auto& uc = j["updateComponents"];
            bool hasSurfaceId = uc.contains("surfaceId") &&
                                uc["surfaceId"].is_string() &&
                                !uc["surfaceId"].get<std::string>().empty();
            checkField(hasSurfaceId, "updateComponents", "surfaceId is non-empty");

            bool hasComponents = uc.contains("components") &&
                                 uc["components"].is_array() &&
                                 !uc["components"].empty();
            checkField(hasComponents, "updateComponents",
                       "components is a non-empty array");
            if(hasComponents)
            {
                // Every component must have an "id" and "component" field.
                bool allValid = true;
                for(const auto& comp : uc["components"])
                {
                    if(!comp.contains("id") || !comp.contains("component"))
                    {
                        allValid = false;
                        break;
                    }
                }
                checkField(allValid, "updateComponents",
                           "all components have id+component (" +
                           std::to_string(uc["components"].size()) + " items)");
            }
        }
        checkVersion(j, "updateComponents");
        return A2ui::Execute_Success;
    }

    A2ui::AGenUIExeCode updateDataModel(const std::string& jsonData) override
    {
        ++_eventCount;
        std::cout << "=== updateDataModel ===" << std::endl;
        std::cout << jsonData << std::endl;

        nlohmann::json j = parseOrFail(jsonData, "updateDataModel");
        if(j.is_null()) return A2ui::Execute_Success;

        checkField(j.contains("updateDataModel"), "updateDataModel", "has 'updateDataModel' key");
        if(j.contains("updateDataModel") && j["updateDataModel"].is_object())
        {
            const auto& dm = j["updateDataModel"];
            bool hasSurfaceId = dm.contains("surfaceId") &&
                                dm["surfaceId"].is_string();
            checkField(hasSurfaceId, "updateDataModel", "surfaceId present");
            bool hasValue = dm.contains("value");
            checkField(hasValue, "updateDataModel", "value present");
        }
        return A2ui::Execute_Success;
    }

    A2ui::AGenUIExeCode appendDataModel(const std::string& jsonData) override
    {
        ++_eventCount;
        std::cout << "=== appendDataModel ===" << std::endl;
        std::cout << jsonData << std::endl;

        nlohmann::json j = parseOrFail(jsonData, "appendDataModel");
        if(j.is_null()) return A2ui::Execute_Success;

        checkField(j.contains("appendDataModel"), "appendDataModel", "has 'appendDataModel' key");
        return A2ui::Execute_Success;
    }

private:
    /// Parse jsonData; on failure record a failed check and return a null json.
    nlohmann::json parseOrFail(const std::string& jsonData, const std::string& eventName)
    {
        try
        {
            auto j = nlohmann::json::parse(jsonData);
            checkField(true, eventName, "jsonData is valid JSON");
            return j;
        }
        catch(const std::exception& e)
        {
            checkField(false, eventName,
                       std::string("jsonData is valid JSON (") + e.what() + ")");
            return nlohmann::json{};
        }
    }

    /// Verify the "version" field equals "v0.9" (if present).
    void checkVersion(const nlohmann::json& j, const std::string& eventName)
    {
        if(j.contains("version") && j["version"].is_string())
        {
            checkField(j["version"] == "v0.9", eventName, "version == 'v0.9'");
        }
    }

    void checkField(bool ok, const std::string& eventName, const std::string& desc)
    {
        _checks.push_back({eventName, desc, ok});
        std::cout << "  [CHECK] " << (ok ? "PASS" : "FAIL") << " — " << desc
                  << std::endl;
    }

    std::vector<CheckResult> _checks;
    int _eventCount = 0;
};


// Reads an entire file into a string.  Returns false on error.
bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream ifs(path, std::ios::binary);
    if(!ifs.is_open())
    {
        std::cerr << "Error: cannot open file: " << path << std::endl;
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

// Converts a JSON array file (e.g. examples/samples/*.json) into a stream of
// individual JSON objects concatenated together.  StreamingContentParser
// expects individual event objects, not a top-level array.  If the file is
// already a single object or JSONL, it is returned unchanged.
std::string FlattenJsonArrayToStream(const std::string& raw)
{
    try
    {
        auto parsed = nlohmann::json::parse(raw);
        if(!parsed.is_array())
        {
            return raw;  // Already a single object or other form — feed as-is.
        }
        std::string stream;
        for(const auto& element : parsed)
        {
            stream += element.dump();
        }
        return stream;
    }
    catch(const std::exception& e)
    {
        // Not valid JSON as a whole (e.g. JSONL) — feed the raw content.
        return raw;
    }
}

} // namespace

int main(int argc, char* argv[])
{
    // Default sample ships with the repo; allow override via command line.
    std::string jsonPath = "../samples/login-form.json";
    if(argc > 1)
    {
        jsonPath = argv[1];
    }

    std::cout << "Streaming parser example" << std::endl;
    std::cout << "Input file: " << jsonPath << std::endl;
    std::cout << "Chunk size: " << kChunkSize << " bytes" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    // 1. Read the JSON file to feed.
    std::string fileContent;
    if(!ReadFile(jsonPath, fileContent))
    {
        return 1;
    }

    // 2. Flatten a JSON array into a concatenation of individual event objects.
    //    The streaming parser expects individual objects, not an array wrapper.
    std::string streamContent = FlattenJsonArrayToStream(fileContent);
    std::cout << "File size: " << fileContent.size() << " bytes"
              << ", stream size: " << streamContent.size() << " bytes" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    // 3. Create the verifying sink and the parser.
    VerifyingSink sink;
    A2ui::StreamingContentParser parser(&sink);

    if(!parser.start())
    {
        std::cerr << "Error: failed to start parser" << std::endl;
        return 1;
    }

    // 4. Begin the streaming session (resets internal extractor state).
    parser.processDataBeginning();

    // 5. Feed the content in fixed-size chunks to simulate the fragmented
    //    arrival of network data.  The extractor uses brace-depth tracking to
    //    detect complete top-level JSON objects, so it correctly handles chunks
    //    that split an object mid-way.
    std::size_t offset = 0;
    int chunkIndex = 0;
    while(offset < streamContent.size())
    {
        std::size_t remaining = streamContent.size() - offset;
        std::size_t take = (remaining < kChunkSize) ? remaining : kChunkSize;
        std::string chunk = streamContent.substr(offset, take);
        parser.processDataAssembling(chunk);
        offset += take;
        ++chunkIndex;
    }
    std::cout << std::string(60, '-') << std::endl;
    std::cout << "Fed " << chunkIndex << " chunks (" << streamContent.size()
              << " bytes total)" << std::endl;

    // 6. End the streaming session.
    parser.processDataEnding();
    std::cout << std::string(60, '-') << std::endl;

    // 7. Print verification summary.
    int passed = 0;
    int failed = 0;
    for(const auto& c : sink.checks())
    {
        if(c.passed) ++passed; else ++failed;
    }
    std::cout << "Verification summary" << std::endl;
    std::cout << "  Events received: " << sink.eventCount() << std::endl;
    std::cout << "  Checks passed:   " << passed << std::endl;
    std::cout << "  Checks failed:   " << failed << std::endl;
    if(failed > 0)
    {
        std::cout << "  --- Failed checks ---" << std::endl;
        for(const auto& c : sink.checks())
        {
            if(!c.passed)
            {
                std::cout << "    [" << c.eventName << "] " << c.description
                          << std::endl;
            }
        }
    }
    std::cout << std::string(60, '-') << std::endl;
    std::cout << (failed == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED")
              << std::endl;

    return (failed == 0) ? 0 : 1;
}
// --- AI-generated section End --[20260817-212800-]-

