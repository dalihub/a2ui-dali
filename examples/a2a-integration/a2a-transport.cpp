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

#include "a2a-transport.h"
#include "core/a2ui-protocol.h"
#include <dali/integration-api/debug.h>
#include <dali-ui-foundation/integration-api/builder/json-parser.h>
#include <curl/curl.h>
#include <cstring>
#include <sstream>

using Dali::Ui::Integration::JsonParser;
using Dali::Ui::Integration::TreeNode;

namespace
{

const char* A2UI_EXTENSION_BASE = "https://a2ui.org/a2a-extension/a2ui";

/**
 * libcurl write callback that appends into a std::string.
 * Used for the synchronous Agent Card GET.
 */
size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

/**
 * Generate a simple pseudo-UUID (good enough for message IDs).
 */
std::string GenerateUuid()
{
  static int counter = 0;
  std::ostringstream ss;
  ss << "dali-" << std::hex << time(nullptr) << "-" << (++counter);
  return ss.str();
}

/**
 * Build the `"metadata":{"a2uiClientCapabilities":{...}}` fragment that is
 * appended inside the message object. Per A2UI spec, the client advertises
 * which basic/custom catalogs it can render natively and whether it can
 * consume inline catalog definitions.
 */
std::string BuildClientCapabilitiesMetadata(
    const std::vector<std::string>& catalogIds,
    bool acceptsInlineCatalogs)
{
  std::ostringstream j;
  j << "\"metadata\":{\"a2uiClientCapabilities\":{"
    << "\"catalogIds\":[";
  for(size_t i = 0; i < catalogIds.size(); ++i)
  {
    if(i > 0) j << ",";
    j << "\"" << catalogIds[i] << "\"";
  }
  j << "],\"acceptsInlineCatalogs\":" << (acceptsInlineCatalogs ? "true" : "false")
    << "}}";
  return j.str();
}

/**
 * Build JSON-RPC 2.0 request for message/stream.
 * If `discoveredExtensionUri` is non-empty, use it verbatim; otherwise
 * construct the URI from `A2UI_EXTENSION_BASE + "/v" + a2uiVersion`.
 */
std::string BuildMessageStreamRequest(const std::string& userMessage,
                                       const std::string& a2uiVersion,
                                       const std::string& contextId,
                                       const std::string& discoveredExtensionUri,
                                       const std::vector<std::string>& catalogIds,
                                       bool acceptsInlineCatalogs)
{
  std::string extensionUri = !discoveredExtensionUri.empty()
                               ? discoveredExtensionUri
                               : std::string(A2UI_EXTENSION_BASE) + "/v" + a2uiVersion;

  // JSON-escape the user message
  std::string escaped;
  for(char c : userMessage)
  {
    if(c == '"') escaped += "\\\"";
    else if(c == '\\') escaped += "\\\\";
    else if(c == '\n') escaped += "\\n";
    else if(c == '\r') escaped += "\\r";
    else if(c == '\t') escaped += "\\t";
    else escaped += c;
  }

  std::string msgId = GenerateUuid();
  std::string capabilities = BuildClientCapabilitiesMetadata(catalogIds, acceptsInlineCatalogs);

  // Build JSON-RPC request
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"message/stream\","
       << "\"params\":{"
       <<   "\"message\":{"
       <<     "\"messageId\":\"" << msgId << "\","
       <<     "\"role\":\"user\","
       <<     "\"parts\":[{\"kind\":\"text\",\"text\":\"" << escaped << "\"}],"
       <<     "\"contextId\":\"" << contextId << "\","
       <<     "\"extensions\":[\"" << extensionUri << "\"],"
       <<     capabilities
       <<   "}"
       << "},"
       << "\"id\":1"
       << "}";

  return json.str();
}

/**
 * Serialize a TreeNode back to a JSON string.
 */
std::string TreeNodeToString(const TreeNode& node)
{
  // Use DALi's JsonParser to serialize
  std::ostringstream ss;

  switch(node.GetType())
  {
    case TreeNode::STRING:
      ss << "\"";
      for(const char* p = node.GetString(); *p; ++p)
      {
        if(*p == '"') ss << "\\\"";
        else if(*p == '\\') ss << "\\\\";
        else if(*p == '\n') ss << "\\n";
        else ss << *p;
      }
      ss << "\"";
      break;

    case TreeNode::INTEGER:
      ss << node.GetInteger();
      break;

    case TreeNode::FLOAT:
      ss << node.GetFloat();
      break;

    case TreeNode::BOOLEAN:
      ss << (node.GetBoolean() ? "true" : "false");
      break;

    case TreeNode::IS_NULL:
      ss << "null";
      break;

    case TreeNode::OBJECT:
    {
      ss << "{";
      bool first = true;
      for(auto it = node.CBegin(); it != node.CEnd(); ++it)
      {
        if(!first) ss << ",";
        first = false;
        ss << "\"" << (*it).first << "\":" << TreeNodeToString((*it).second);
      }
      ss << "}";
      break;
    }

    case TreeNode::ARRAY:
    {
      ss << "[";
      bool first = true;
      for(auto it = node.CBegin(); it != node.CEnd(); ++it)
      {
        if(!first) ss << ",";
        first = false;
        ss << TreeNodeToString((*it).second);
      }
      ss << "]";
      break;
    }
  }

  return ss.str();
}

} // anonymous namespace

namespace A2ui
{

A2aTransportAdapter::A2aTransportAdapter()
{
}

A2aTransportAdapter::~A2aTransportAdapter()
{
  Disconnect();
}

void A2aTransportAdapter::SetServerUrl(const std::string& url)
{
  std::string normalized = url;
  if(!normalized.empty() && normalized.back() == '/')
  {
    normalized.pop_back();
  }
  if(normalized == mServerUrl) return;

  // Switching servers: kill any in-flight stream so worker callbacks can't
  // splice responses from the old server into the new session. Disconnect()
  // clears mRunning and joins both threads.
  Disconnect();

  mServerUrl = normalized;
  mAgentCardFetched = false;
  mDiscoveredExtensionUri.clear();
  mRpcEndpoint.clear();
  mSupportedCatalogIds.clear();
  mAcceptsInlineCatalogs = false;
  mContextId.clear();
  mSseBuffer.clear();
  mCompleteFired = false;
  mFirstDataReceived = false;

  DALI_LOG_ERROR("[A2A] Server URL changed → %s (caches + session reset)\n",
                 mServerUrl.c_str());
}

void A2aTransportAdapter::ResetSession()
{
  mContextId.clear();
}

/**
 * Setting an explicit version invalidates any previously discovered
 * extension URI so the next `SendMessage` re-runs `FetchAgentCard` and
 * picks the URI that matches the newly-requested version.
 */
void A2aTransportAdapter::SetA2uiVersion(const std::string& version)
{
  if(mA2uiVersion == version) return;
  mA2uiVersion = version;
  // Force re-discovery so the next SendMessage picks the URI that matches.
  mAgentCardFetched = false;
  mDiscoveredExtensionUri.clear();
  mSupportedCatalogIds.clear();
  mAcceptsInlineCatalogs = false;
}

bool A2aTransportAdapter::FetchAgentCard()
{
  if(mServerUrl.empty())
  {
    DALI_LOG_ERROR("[A2A] FetchAgentCard: server URL not set\n");
    return false;
  }

  std::string url = mServerUrl + "/.well-known/agent-card.json";
  DALI_LOG_ERROR("[A2A] Fetching agent card: %s\n", url.c_str());

  CURL* curl = curl_easy_init();
  if(!curl) return false;

  std::string body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_easy_cleanup(curl);

  if(res != CURLE_OK || httpCode < 200 || httpCode >= 300)
  {
    DALI_LOG_ERROR("[A2A] Agent card fetch failed: curl=%d http=%ld\n", (int)res, httpCode);
    return false;
  }

  JsonParser parser = JsonParser::New();
  if(!parser.Parse(body))
  {
    DALI_LOG_ERROR("[A2A] Agent card JSON parse failed\n");
    return false;
  }

  const TreeNode* root = parser.GetRoot();
  if(!root) return false;

  // Reset discovered state
  mDiscoveredExtensionUri.clear();
  mRpcEndpoint.clear();
  mSupportedCatalogIds.clear();
  mAcceptsInlineCatalogs = false;

  // Per A2A, AgentCard.url is the JSON-RPC endpoint itself (no conventional
  // sub-path like `/rpc`). Adopt it if present.
  const TreeNode* urlNode = root->Find("url");
  if(urlNode && urlNode->GetType() == TreeNode::STRING)
  {
    mRpcEndpoint = urlNode->GetString();
  }

  const std::string preferredVersion = mA2uiVersion; // e.g. "0.9"

  // Walk capabilities.extensions[]. Servers can advertise multiple A2UI
  // versions (restaurant_finder exposes both v0.8 and v0.9). Pick the
  // entry whose version matches our preferred `mA2uiVersion` first; if no
  // match, fall back to the first A2UI extension found.
  const TreeNode* capabilities = root->Find("capabilities");
  const TreeNode* extensions   = capabilities ? capabilities->Find("extensions") : nullptr;
  const TreeNode* chosenExt    = nullptr;
  std::string     chosenUri;
  std::string     chosenVersion;

  auto parseVersion = [](const std::string& uri) -> std::string {
    std::string prefix = std::string(A2UI_EXTENSION_BASE) + "/v";
    if(uri.rfind(prefix, 0) != 0) return "";
    std::string ver;
    for(size_t i = prefix.size(); i < uri.size(); ++i)
    {
      char c = uri[i];
      if((c >= '0' && c <= '9') || c == '.') ver += c;
      else break;
    }
    return ver;
  };

  if(extensions && extensions->GetType() == TreeNode::ARRAY)
  {
    const TreeNode* firstA2ui    = nullptr;
    std::string     firstA2uiUri, firstA2uiVer;

    for(auto it = extensions->CBegin(); it != extensions->CEnd(); ++it)
    {
      const TreeNode& ext = (*it).second;
      const TreeNode* uriNode = ext.Find("uri");
      if(!uriNode || uriNode->GetType() != TreeNode::STRING) continue;

      std::string uri = uriNode->GetString();
      if(uri.rfind(A2UI_EXTENSION_BASE, 0) != 0) continue;

      std::string ver = parseVersion(uri);

      // Preferred version wins immediately.
      if(!preferredVersion.empty() && ver == preferredVersion)
      {
        chosenExt     = &ext;
        chosenUri     = uri;
        chosenVersion = ver;
        break;
      }

      // Remember the first A2UI entry as a fallback.
      if(!firstA2ui)
      {
        firstA2ui    = &ext;
        firstA2uiUri = uri;
        firstA2uiVer = ver;
      }
    }

    if(!chosenExt && firstA2ui)
    {
      chosenExt     = firstA2ui;
      chosenUri     = firstA2uiUri;
      chosenVersion = firstA2uiVer;
    }
  }

  if(chosenExt)
  {
    mDiscoveredExtensionUri = chosenUri;
    if(!chosenVersion.empty()) mA2uiVersion = chosenVersion;

    // params: supportedCatalogIds[], acceptsInlineCatalogs
    const TreeNode* params = chosenExt->Find("params");
    if(params)
    {
      const TreeNode* catalogs = params->Find("supportedCatalogIds");
      if(catalogs && catalogs->GetType() == TreeNode::ARRAY)
      {
        for(auto ct = catalogs->CBegin(); ct != catalogs->CEnd(); ++ct)
        {
          const TreeNode& c = (*ct).second;
          if(c.GetType() == TreeNode::STRING)
          {
            mSupportedCatalogIds.emplace_back(c.GetString());
          }
        }
      }
      const TreeNode* accepts = params->Find("acceptsInlineCatalogs");
      if(accepts && accepts->GetType() == TreeNode::BOOLEAN)
      {
        mAcceptsInlineCatalogs = accepts->GetBoolean();
      }
    }
  }

  mAgentCardFetched = !mDiscoveredExtensionUri.empty();
  DALI_LOG_ERROR("[A2A] Agent card: ext=%s, catalogs=%zu, inline=%d\n",
                 mDiscoveredExtensionUri.c_str(),
                 mSupportedCatalogIds.size(),
                 mAcceptsInlineCatalogs ? 1 : 0);
  return mAgentCardFetched;
}

void A2aTransportAdapter::SendMessage(const std::string& userMessage)
{
  Disconnect();

  mRunning = true;
  mCompleteFired = false;
  mFirstDataReceived = false;

  // Lazy one-shot AgentCard discovery. Failure is non-fatal:
  // we fall back to the configured mA2uiVersion.
  if(!mAgentCardFetched)
  {
    FetchAgentCard();
  }

  if(mContextId.empty())
  {
    mContextId = GenerateUuid();
  }

  std::string rpcEndpoint = !mRpcEndpoint.empty()
                              ? mRpcEndpoint
                              : mServerUrl + "/rpc";
  std::string body = BuildMessageStreamRequest(userMessage, mA2uiVersion,
                                                mContextId, mDiscoveredExtensionUri,
                                                mSupportedCatalogIds,
                                                mAcceptsInlineCatalogs);

  DALI_LOG_ERROR("[A2A] Sending message/stream to %s\n", rpcEndpoint.c_str());

  mWorkerThread = std::thread(&A2aTransportAdapter::WorkerThread, this, rpcEndpoint, body);
}

void A2aTransportAdapter::SendAction(const std::string& actionJson)
{
  // Dispatch the HTTP round-trip on a background thread so the DALi event
  // thread (which is calling us from a Button tap handler) stays responsive:
  // the status bar can update, the skeleton/render can animate, further
  // taps and keystrokes keep flowing. Join any prior action thread first so
  // we don't leak or race two outstanding requests.
  if(mActionThread.joinable())
  {
    mActionThread.join();
  }
  // Reset the latch so the terminal event of THIS action's response fires
  // mOnComplete. Without this, the latch stays true after the initial
  // SendMessage completes and every subsequent action looks "not done" to
  // any logic gated on completion (e.g. the status-banner green transition).
  mCompleteFired = false;
  mActionThread = std::thread(&A2aTransportAdapter::SendActionSync, this, actionJson);
}

void A2aTransportAdapter::SendActionSync(std::string actionJson)
{
  // Send a client action via message/send (synchronous HTTP).
  // actionJson is the full A2UI action message body (data part payload).
  CURL* curl = curl_easy_init();
  if(!curl) return;

  // Lazy one-shot Agent Card discovery, in case Send* is called for the
  // first time with SendAction (rare — usually SendMessage runs first).
  if(!mAgentCardFetched)
  {
    FetchAgentCard();
  }

  if(mContextId.empty())
  {
    mContextId = GenerateUuid();
  }

  std::string rpcEndpoint = !mRpcEndpoint.empty()
                              ? mRpcEndpoint
                              : mServerUrl + "/rpc";

  std::string extensionUri = !mDiscoveredExtensionUri.empty()
                               ? mDiscoveredExtensionUri
                               : std::string(A2UI_EXTENSION_BASE) + "/v" + mA2uiVersion;
  std::string capabilities = BuildClientCapabilitiesMetadata(
      mSupportedCatalogIds, mAcceptsInlineCatalogs);

  // Build JSON-RPC message/send with the action as a DataPart.
  // Include A2UI extension URI + client capabilities so the server knows
  // this is an A2UI-protocol client even though the method is message/send.
  std::ostringstream json;
  json << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"method\":\"message/send\","
       << "\"params\":{"
       <<   "\"message\":{"
       <<     "\"messageId\":\"" << GenerateUuid() << "\","
       <<     "\"role\":\"user\","
       <<     "\"parts\":[{\"kind\":\"data\",\"data\":" << actionJson
       <<        ",\"metadata\":{\"mimeType\":\"" << A2ui::A2UI_MIME_TYPE << "\"}}],"
       <<     "\"contextId\":\"" << mContextId << "\","
       <<     "\"extensions\":[\"" << extensionUri << "\"],"
       <<     capabilities
       <<   "}"
       << "},"
       << "\"id\":2"
       << "}";

  std::string body = json.str();

  curl_easy_setopt(curl, CURLOPT_URL, rpcEndpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  // Capture the response body. Without this, curl writes the body to stdout
  // by default, which (a) drops the A2UI DataParts on the floor and (b) has
  // been observed to interact badly with DALi's main loop (SIGPIPE-style
  // termination right after the tap). Feed the captured body through the
  // same ProcessA2aEvent path used by the SSE worker so the server's
  // booking-form response reaches StreamingController.
  std::string responseBody;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
    +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
      size_t n = size * nmemb;
      static_cast<std::string*>(userdata)->append(ptr, n);
      return n;
    });
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string extHeader = "X-A2A-Extensions: " + extensionUri;
  headers = curl_slist_append(headers, extHeader.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  if(res != CURLE_OK)
  {
    std::string err = "A2A SendAction error: ";
    err += curl_easy_strerror(res);
    DALI_LOG_ERROR("[A2A] %s\n", err.c_str());
    if(mOnError) mOnError(err);
  }
  else
  {
    DALI_LOG_ERROR("[A2A] SendAction response len=%zu\n", responseBody.size());
    if(!responseBody.empty())
    {
      // ProcessA2aEvent expects a single JSON-RPC response (or a status-update
      // event). It unwraps `result` and extracts DataParts from
      // `status.message.parts[]`, invoking mOnChunk for each. That's exactly
      // what restaurant_finder returns for a `message/send` action, so the
      // same code path works here.
      ProcessA2aEvent(responseBody);
    }
  }
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
}

void A2aTransportAdapter::Disconnect()
{
  mRunning = false;
  if(mWorkerThread.joinable())
  {
    mWorkerThread.join();
  }
  if(mActionThread.joinable())
  {
    mActionThread.join();
  }
}

bool A2aTransportAdapter::IsConnected() const
{
  return mRunning;
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

size_t A2aTransportAdapter::WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* self = static_cast<A2aTransportAdapter*>(userdata);
  if(!self->mRunning) return 0;

  size_t totalSize = size * nmemb;

  if(!self->mFirstDataReceived)
  {
    self->mFirstDataReceived = true;
    if(self->mOnConnected) self->mOnConnected();
  }

  std::string chunk(ptr, totalSize);
  self->ProcessSseChunk(chunk);

  return totalSize;
}

void A2aTransportAdapter::ProcessSseChunk(const std::string& chunk)
{
  mSseBuffer += chunk;

  // Guard against a stalled/malformed server that never emits a delimiter —
  // bound the buffer so it can't grow indefinitely.
  constexpr size_t kMaxSseBuffer = 16 * 1024 * 1024;
  if(mSseBuffer.size() > kMaxSseBuffer)
  {
    DALI_LOG_ERROR("[A2A] SSE buffer exceeded %zu bytes without delimiter; aborting\n",
                   kMaxSseBuffer);
    if(mOnError) mOnError("SSE buffer overflow: server sent no event delimiter");
    mSseBuffer.clear();
    mRunning = false;
    return;
  }

  // SSE events terminate with either `\n\n` or `\r\n\r\n`. uvicorn/Starlette
  // emits CRLF line endings by default, so we must handle both.
  auto findDelim = [&](size_t from, size_t& delimLen) -> size_t {
    size_t crlf = mSseBuffer.find("\r\n\r\n", from);
    size_t lfn  = mSseBuffer.find("\n\n",    from);
    if(crlf != std::string::npos &&
       (lfn == std::string::npos || crlf <= lfn))
    {
      delimLen = 4;
      return crlf;
    }
    delimLen = 2;
    return lfn;
  };

  size_t pos = 0;
  size_t eventEnd;
  size_t delimLen = 2;
  while((eventEnd = findDelim(pos, delimLen)) != std::string::npos)
  {
    std::string event = mSseBuffer.substr(pos, eventEnd - pos);
    pos = eventEnd + delimLen;

    // Extract "data:" lines. Trim a trailing '\r' when present so that CRLF
    // line endings are handled the same as LF.
    size_t lineStart = 0;
    std::string eventData;
    while(lineStart < event.size())
    {
      size_t lineEnd = event.find('\n', lineStart);
      if(lineEnd == std::string::npos) lineEnd = event.size();

      size_t lineContentEnd = lineEnd;
      if(lineContentEnd > lineStart && event[lineContentEnd - 1] == '\r')
      {
        lineContentEnd--;
      }
      std::string line = event.substr(lineStart, lineContentEnd - lineStart);
      lineStart = lineEnd + 1;

      if(line.compare(0, 6, "data: ") == 0)
      {
        eventData += line.substr(6);
      }
      else if(line.compare(0, 5, "data:") == 0)
      {
        eventData += line.substr(5);
      }
    }

    if(!eventData.empty())
    {
      ProcessA2aEvent(eventData);
    }
  }

  mSseBuffer = mSseBuffer.substr(pos);
}

void A2aTransportAdapter::ProcessA2aEvent(const std::string& eventData)
{
  // Parse the A2A event JSON
  JsonParser parser = JsonParser::New();
  if(!parser.Parse(eventData))
  {
    DALI_LOG_ERROR("[A2A] Failed to parse event: %s\n", eventData.substr(0, 100).c_str());
    return;
  }

  const TreeNode* root = parser.GetRoot();
  if(!root) return;

  // Unwrap JSON-RPC 2.0 envelope: `{"jsonrpc":"2.0","result":{...},"id":N}`.
  // Actual A2A event fields (`kind`, `status`, `final`, ...) live inside
  // `result`. Fall back to `root` for servers that skip the envelope.
  const TreeNode* resultNode = root->Find("result");
  const TreeNode* event      = resultNode ? resultNode : root;

  // JSON-RPC error? Surface it.
  const TreeNode* errorNode = root->Find("error");
  if(errorNode && errorNode->GetType() == TreeNode::OBJECT)
  {
    const TreeNode* msg = errorNode->Find("message");
    std::string em = (msg && msg->GetType() == TreeNode::STRING) ? msg->GetString() : "JSON-RPC error";
    DALI_LOG_ERROR("[A2A] %s\n", em.c_str());
    if(mOnError) mOnError(em);
    return;
  }

  // Check event kind
  const TreeNode* kindNode = event->Find("kind");
  std::string kind;
  if(kindNode && kindNode->GetType() == TreeNode::STRING)
  {
    kind = kindNode->GetString();
  }

  // Initial task event kinds carry no `kind` field (spec versions older than
  // v0.3) or `kind:"task"` (v0.3+). Log and continue.
  if(kind.empty() || kind == "task")
  {
    const TreeNode* idNode = event->Find("id");
    if(idNode)
    {
      DALI_LOG_ERROR("[A2A] Task: %s\n",
                     idNode->GetType() == TreeNode::STRING ? idNode->GetString() : "?");
    }
    // A terminal task (state:"completed"/"input-required"/"failed") may also
    // come with a message payload — fall through to let the status-update
    // branch mine it if present.
    const TreeNode* taskStatus = event->Find("status");
    if(kind == "task" && taskStatus && taskStatus->GetType() == TreeNode::OBJECT)
    {
      // Extract parts from terminal-task.status.message if any (agent-side
      // final aggregation). Reuse the same extraction path below.
    }
    else
    {
      return;
    }
  }

  if(kind == "status-update" || kind == "task")
  {
    const TreeNode* statusNode = event->Find("status");
    if(!statusNode) return;

    // Check state
    const TreeNode* stateNode = statusNode->Find("state");
    std::string state;
    if(stateNode && stateNode->GetType() == TreeNode::STRING)
    {
      state = stateNode->GetString();
    }
    DALI_LOG_ERROR("[A2A] Status: %s\n", state.c_str());

    // Extract message parts
    const TreeNode* messageNode = statusNode->Find("message");
    if(!messageNode) return;

    const TreeNode* partsNode = messageNode->Find("parts");
    if(!partsNode || partsNode->GetType() != TreeNode::ARRAY) return;

    // Iterate parts: extract A2UI DataParts
    for(auto it = partsNode->CBegin(); it != partsNode->CEnd(); ++it)
    {
      const TreeNode& part = (*it).second;

      const TreeNode* partKind = part.Find("kind");
      if(!partKind || partKind->GetType() != TreeNode::STRING) continue;

      if(std::string(partKind->GetString()) == "data")
      {
        // Check mimeType. Absent or matching → treat as A2UI DataPart.
        bool isA2uiPart = true;
        const TreeNode* metadata = part.Find("metadata");
        if(metadata)
        {
          const TreeNode* mimeType = metadata->Find("mimeType");
          if(mimeType && mimeType->GetType() == TreeNode::STRING)
          {
            isA2uiPart = A2ui::IsA2uiMimeType(mimeType->GetString());
          }
        }
        if(!isA2uiPart) continue;

        const TreeNode* dataNode = part.Find("data");
        if(dataNode)
        {
          std::string jsonLine = TreeNodeToString(*dataNode);
          DALI_LOG_ERROR("[A2A] A2UI DataPart (full len=%zu): %s\n",
                         jsonLine.size(), jsonLine.c_str());

          if(mOnChunk)
          {
            mOnChunk(jsonLine + "\n");
          }
        }
      }
      else if(std::string(partKind->GetString()) == "text")
      {
        const TreeNode* textNode = part.Find("text");
        if(textNode && textNode->GetType() == TreeNode::STRING)
        {
          std::string agentText = textNode->GetString();
          DALI_LOG_ERROR("[A2A] Agent text: %s\n", agentText.c_str());
          if(mOnAgentMessage)
          {
            mOnAgentMessage(agentText);
          }
        }
      }
    }

    // Check if this is the final event
    const TreeNode* finalNode = event->Find("final");
    if(finalNode && finalNode->GetType() == TreeNode::BOOLEAN && finalNode->GetBoolean())
    {
      DALI_LOG_ERROR("[A2A] Stream complete (final=true)\n");
      if(!mCompleteFired.exchange(true))
      {
        if(mOnComplete) mOnComplete();
      }
    }
    else if(state == "input-required" || state == "completed" || state == "failed")
    {
      // Some servers use terminal state without `final:true`.
      DALI_LOG_ERROR("[A2A] Stream ends (state=%s)\n", state.c_str());
      if(!mCompleteFired.exchange(true))
      {
        if(mOnComplete) mOnComplete();
      }
    }
  }
}

void A2aTransportAdapter::WorkerThread(const std::string& rpcEndpoint,
                                        const std::string& jsonRpcBody)
{
  DALI_LOG_ERROR("[A2A] Connecting to: %s\n", rpcEndpoint.c_str());

  CURL* curl = curl_easy_init();
  if(!curl)
  {
    if(mOnError) mOnError("Failed to initialize curl");
    mRunning = false;
    return;
  }

  mSseBuffer.clear();

  curl_easy_setopt(curl, CURLOPT_URL, rpcEndpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, jsonRpcBody.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

  // Headers
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: text/event-stream");

  // A2A extension header — prefer the URI discovered via Agent Card (if any)
  std::string extUri = !mDiscoveredExtensionUri.empty()
                         ? mDiscoveredExtensionUri
                         : std::string(A2UI_EXTENSION_BASE) + "/v" + mA2uiVersion;
  std::string extHeader = "X-A2A-Extensions: " + extUri;
  headers = curl_slist_append(headers, extHeader.c_str());

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  if(res != CURLE_OK && mRunning)
  {
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    std::string err = "A2A error: ";
    err += curl_easy_strerror(res);
    err += " (http=" + std::to_string(httpCode) + ")";
    DALI_LOG_ERROR("[A2A] %s\n", err.c_str());
    if(mOnError) mOnError(err);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if(!mCompleteFired.exchange(true))
  {
    if(mOnComplete) mOnComplete();
  }
  mRunning = false;

  DALI_LOG_ERROR("[A2A] Disconnected\n");
}

} // namespace A2ui
