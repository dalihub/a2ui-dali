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

// Streaming parser example (ThreadManager version).
//
//   ./a2ui-streaming-parser [path/to/messages.json]
//
// Reads an A2UI v0.9 JSON file (default: examples/samples/login-form.json, looked
// up relative to the current directory and to the executable, so it works from
// the repo root, build/ or bin/) and feeds it in fixed-size chunks through
// StreamingContentParser, which extracts each message and dispatches it to an
// IStreamEventSink implementation that prints the jsonData to stdout.
//
// The stream is fed twice: once as concatenated objects and once as JSONL
// (one message per line), which is the usual wire format.
//
// The file reading and chunk-feeding work is posted to a worker thread managed
// by ThreadManager, demonstrating the thread infrastructure ported from AGenUI.
// The main thread blocks on a std::promise until the worker finishes, then
// prints the verification summary.
//
// In addition to printing, the sink *verifies* each parsed event: it
// re-deserializes the jsonData with nlohmann/json, checks that it is valid
// JSON with the expected top-level key (e.g. "createSurface"), and asserts
// key fields (version, surfaceId, components array, data model value).
// After each pass the received events are compared with the source messages
// (createSurface/deleteSurface counts, component ids). A summary of PASS/FAIL
// checks is printed; the exit code is non-zero if the input cannot be read or
// any check fails.
//
// StreamingContentParser expects a stream of *individual* A2UI event objects
// (JSONL-style), not a top-level JSON array.  The sample files under
// examples/samples/ are JSON arrays, so this example flattens the array into its
// element objects before feeding them to the parser — exactly how an A2UI
// transport would deliver the events.
//
// Pipeline:
//   StreamingContentParser -> ProtocolStreamExtractor -> IStreamEventSink

#include "stream/streaming-content-parser.h"
#include "stream/stream-event-sink.h"
#include "stream/error-code.h"
#include "module/thread-manager.h"

#include <nlohmann/json.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>


namespace
{

// Size of each chunk fed to the parser to simulate streaming arrival.
constexpr std::size_t kChunkSize = 64;

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

    /// Per-pass bookkeeping used to compare the received events with the source.
    int createCount = 0;
    int deleteCount = 0;
    std::set<std::string> componentIds;

    void resetPass()
    {
        createCount = 0;
        deleteCount = 0;
        componentIds.clear();
    }

    /// Records a check performed outside a sink callback.
    void check(bool ok, const std::string& eventName, const std::string& desc)
    {
        checkField(ok, eventName, desc);
    }

    A2ui::AGenUIExeCode createSurface(const std::string& jsonData) override
    {
        ++_eventCount;
        std::cout << "=== createSurface ===" << std::endl;
        std::cout << jsonData << std::endl;

        ++createCount;
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

        ++deleteCount;
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
                    if(comp["id"].is_string())
                    {
                        componentIds.insert(comp["id"].get<std::string>());
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

// Splits a JSON array file (e.g. examples/samples/*.json) or a JSONL file into
// its individual messages. Returns false if the content is neither.
bool LoadMessages(const std::string& raw, std::vector<nlohmann::json>& out)
{
    nlohmann::json parsed = nlohmann::json::parse(raw, nullptr, false);
    if(!parsed.is_discarded())
    {
        if(parsed.is_array())
        {
            for(const auto& element : parsed)
            {
                out.push_back(element);
            }
        }
        else
        {
            out.push_back(parsed);
        }
        return !out.empty();
    }
    std::istringstream lines(raw);
    std::string line;
    while(std::getline(lines, line))
    {
        if(line.find_first_not_of(" \t\r") == std::string::npos)
        {
            continue;
        }
        nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
        if(message.is_discarded())
        {
            return false;
        }
        out.push_back(message);
    }
    return !out.empty();
}

// Returns the directory holding this executable ("" if unknown).
std::string ExecutableDir()
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if(n <= 0)
    {
        return "";
    }
    std::string path(buf, static_cast<std::size_t>(n));
    std::size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? "" : path.substr(0, slash);
}

// Default input: examples/samples/login-form.json, from the repo root, build/, bin/,
// or next to the installed data (samples/).
std::string DefaultInputPath()
{
    const std::string rel = "examples/samples/login-form.json";
    std::vector<std::string> candidates = {rel, "../" + rel};
    std::string exeDir = ExecutableDir();
    if(!exeDir.empty())
    {
        candidates.push_back(exeDir + "/../" + rel);
    }
    for(const auto& c : candidates)
    {
        if(std::ifstream(c).good())
        {
            return c;
        }
    }
    return rel;
}

} // namespace

// Thread ID used for the worker thread that loads and parses the JSON file.
constexpr int kWorkerThreadId = 1;

int main(int argc, char* argv[])
{
    // Default sample ships with the repo; allow override via command line.
    std::string jsonPath = (argc > 1) ? argv[1] : DefaultInputPath();

    std::cout << "Streaming parser example (ThreadManager)" << std::endl;
    std::cout << "Input file: " << jsonPath << std::endl;
    std::cout << "Chunk size: " << kChunkSize << " bytes" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    std::string fileContent;
    if(!ReadFile(jsonPath, fileContent))
    {
        return 1;
    }
    std::vector<nlohmann::json> messages;
    if(!LoadMessages(fileContent, messages))
    {
        std::cerr << "Error: " << jsonPath << " is neither a JSON array of messages nor JSONL" << std::endl;
        return 1;
    }

    // What the source contains, to compare with what the parser delivers.
    int expectedCreates = 0;
    int expectedDeletes = 0;
    std::set<std::string> expectedIds;
    std::string concatenated;
    std::string jsonl;
    for(const auto& m : messages)
    {
        if(m.contains("createSurface")) ++expectedCreates;
        if(m.contains("deleteSurface")) ++expectedDeletes;
        if(m.contains("updateComponents") && m["updateComponents"].contains("components"))
        {
            for(const auto& comp : m["updateComponents"]["components"])
            {
                if(comp.contains("id") && comp["id"].is_string()) expectedIds.insert(comp["id"].get<std::string>());
            }
        }
        concatenated += m.dump();
        jsonl += m.dump() + "\n";
    }

    // 1. Create a worker thread via ThreadManager.
    auto& tm = A2ui::ThreadManager::getInstance();
    if(!tm.createThread(kWorkerThreadId))
    {
        std::cerr << "Error: failed to create worker thread" << std::endl;
        return 1;
    }
    A2ui::IThread* workerThread = tm.getMessageThread(kWorkerThreadId);
    if(!workerThread)
    {
        std::cerr << "Error: worker thread is null" << std::endl;
        tm.destroyThread(kWorkerThreadId);
        return 1;
    }

    // 2. Create the verifying sink and the parser on the main thread.
    //    The sink and parser are safe to share because the worker only calls
    //    parser methods, and the main thread waits for completion before
    //    reading the sink's results.
    VerifyingSink sink;
    A2ui::StreamingContentParser parser(&sink);

    if(!parser.start())
    {
        std::cerr << "Error: failed to start parser" << std::endl;
        tm.destroyThread(kWorkerThreadId);
        return 1;
    }

    struct Pass { const char* label; const std::string* stream; };
    const Pass passes[] = {{"concatenated objects", &concatenated}, {"JSONL", &jsonl}};
    for(const auto& pass : passes)
    {
        std::cout << "=== Pass: " << pass.label << " (" << pass.stream->size() << " bytes) ===" << std::endl;
        sink.resetPass();

        // 3. Use a promise to signal completion from the worker thread.
        std::promise<void> donePromise;
        std::future<void> doneFuture = donePromise.get_future();

        // 4. Post the chunk-feeding work to the worker thread.
        const std::string& streamContent = *pass.stream;
        workerThread->post([&]() {
            parser.processDataBeginning();

            // Feed the content in fixed-size chunks to simulate fragmented arrival.
            std::size_t offset = 0;
            int chunkIndex = 0;
            while(offset < streamContent.size())
            {
                std::size_t take = std::min(kChunkSize, streamContent.size() - offset);
                parser.processDataAssembling(streamContent.substr(offset, take));
                offset += take;
                ++chunkIndex;
            }
            std::cout << "[Worker] Fed " << chunkIndex << " chunks" << std::endl;

            parser.processDataEnding();
            donePromise.set_value();
        });

        // 5. Main thread waits for the worker to finish.
        doneFuture.get();

        // 6. Compare what arrived with the source messages.
        std::string tag = std::string("pass: ") + pass.label;
        sink.check(sink.createCount == expectedCreates, tag,
                   "createSurface events: " + std::to_string(sink.createCount) + " of " + std::to_string(expectedCreates));
        sink.check(sink.deleteCount == expectedDeletes, tag,
                   "deleteSurface events: " + std::to_string(sink.deleteCount) + " of " + std::to_string(expectedDeletes));
        sink.check(sink.componentIds == expectedIds, tag,
                   "component ids: " + std::to_string(sink.componentIds.size()) + " of " + std::to_string(expectedIds.size()));
        std::cout << std::string(60, '-') << std::endl;
    }

    // 7. Stop the parser and destroy the worker thread.
    parser.stop();
    tm.destroyThread(kWorkerThreadId);

    // 8. Print verification summary on the main thread.
    int passed = 0;
    int failed = 0;
    for(const auto& c : sink.checks())
    {
        if(c.passed) ++passed; else ++failed;
    }
    if(sink.eventCount() == 0)
    {
        ++failed;
        std::cout << "  No events were received" << std::endl;
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
