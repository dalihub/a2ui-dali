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

#ifndef DALI_GENERATIVE_UI_A2A_TRANSPORT_H
#define DALI_GENERATIVE_UI_A2A_TRANSPORT_H

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <functional>

namespace A2ui
{

/**
 * A2A (Agent-to-Agent) protocol transport.
 *
 * Implements the A2A JSON-RPC 2.0 + SSE streaming protocol:
 * 1. Fetches Agent Card from /.well-known/agent-card.json
 * 2. Sends message/stream via POST /rpc with JSON-RPC envelope
 * 3. Parses SSE status-update events
 * 4. Extracts A2UI DataParts (application/a2ui+json) from agent messages
 * 5. Forwards extracted A2UI JSON as JSONL lines via OnChunk callback
 *
 * Usage:
 *   A2aTransportAdapter a2a;
 *   a2a.SetServerUrl("http://localhost:10003");
 *   a2a.SetOnChunk([](const std::string& data) { ... });
 *   a2a.SendMessage("Show me a weather card");
 *   // later, on user interaction:
 *   a2a.SendAction("{\"name\":\"book_restaurant\", ...}");
 */
class A2aTransportAdapter
{
public:
  using ChunkCallback        = std::function<void(const std::string& data)>;
  using ErrorCallback        = std::function<void(const std::string& error)>;
  using ConnectedCallback    = std::function<void()>;
  using CompleteCallback     = std::function<void()>;
  using AgentMessageCallback = std::function<void(const std::string& text)>;

  A2aTransportAdapter();
  ~A2aTransportAdapter();

  /** Set the A2A server base URL (e.g. "http://localhost:10003"). */
  void SetServerUrl(const std::string& url);

  /** Set the A2UI extension version to request (default: "0.9"). */
  void SetA2uiVersion(const std::string& version);

  /**
   * Fetch `{serverUrl}/.well-known/agent-card.json`, parse the A2A Agent Card,
   * and populate discovered capabilities (extension URI, supported catalog IDs,
   * acceptsInlineCatalogs, skills).
   *
   * Synchronous; returns true on success. On failure the transport keeps
   * its default extension URI (v{mA2uiVersion}) and proceeds — the server
   * will either accept or reject the request on its own merits.
   *
   * Safe to call multiple times; re-fetches.
   */
  bool FetchAgentCard();

  /** True once FetchAgentCard() has succeeded at least once. */
  bool HasAgentCard() const { return mAgentCardFetched; }

  /** Discovered A2UI extension URI (e.g. "https://a2ui.org/a2a-extension/a2ui/v0.9"). */
  const std::string& GetDiscoveredExtensionUri() const { return mDiscoveredExtensionUri; }

  /** Supported catalog IDs advertised by the server. */
  const std::vector<std::string>& GetSupportedCatalogIds() const { return mSupportedCatalogIds; }

  /** True if the server accepts inline catalog definitions in client capabilities. */
  bool GetAcceptsInlineCatalogs() const { return mAcceptsInlineCatalogs; }

  /**
   * Send a user message via A2A message/stream.
   * Starts SSE streaming; A2UI DataParts are extracted and forwarded as JSONL.
   */
  void SendMessage(const std::string& userMessage);

  /**
   * Send a client action (A2UI message/send) as a DataPart.
   * The `actionJson` should be the A2UI `action` JSON body
   * (e.g. `{"action":{"name":"book_restaurant","context":{...}}}`).
   * Synchronous; response ignored (server streams next UI via the
   * originating message/stream channel).
   */
  void SendAction(const std::string& actionJson);

  /** Disconnect and join the worker thread. */
  void Disconnect();

  /** True while the SSE worker thread is running. */
  bool IsConnected() const;

  /**
   * Clear the A2A session (`contextId`) so the next `SendMessage` /
   * `SendAction` starts a fresh conversation. Does not touch the server URL
   * or the discovered AgentCard. Use when the same agent needs to handle a
   * new, unrelated request.
   */
  void ResetSession();

  void SetOnChunk(ChunkCallback cb)              { mOnChunk = std::move(cb); }
  void SetOnError(ErrorCallback cb)              { mOnError = std::move(cb); }
  void SetOnConnected(ConnectedCallback cb)      { mOnConnected = std::move(cb); }
  void SetOnComplete(CompleteCallback cb)        { mOnComplete = std::move(cb); }
  /** Fired from the worker thread whenever the agent emits a text message part
   *  (A2A status-update with kind:"text"). Use this to drive a "thinking"
   *  overlay; marshal to the main thread if touching DALi. */
  void SetOnAgentMessage(AgentMessageCallback cb) { mOnAgentMessage = std::move(cb); }

private:
  void WorkerThread(const std::string& rpcEndpoint, const std::string& jsonRpcBody);

  /** libcurl write callback — receives SSE data chunks. */
  static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

  /** Parse SSE framing and extract A2A events. */
  void ProcessSseChunk(const std::string& chunk);

  /** Parse a single A2A SSE event and extract A2UI DataParts. */
  void ProcessA2aEvent(const std::string& eventData);

  std::string              mServerUrl;
  std::string              mA2uiVersion = "0.9";
  std::string              mContextId;

  // --- AgentCard discovery state ---
  bool                         mAgentCardFetched = false;
  std::string                  mDiscoveredExtensionUri;
  std::string                  mRpcEndpoint;  // from AgentCard.url, fallback mServerUrl + "/rpc"
  std::vector<std::string>     mSupportedCatalogIds;
  bool                         mAcceptsInlineCatalogs = false;

  /** Internal synchronous implementation of SendAction — runs the curl
   *  round-trip and fans the response out via mOnChunk. SendAction dispatches
   *  this on mActionThread so the caller (usually the DALi event thread
   *  handling a Button tap) is not blocked. */
  void SendActionSync(std::string actionJson);

  std::thread              mWorkerThread;
  std::thread              mActionThread;
  std::atomic<bool>        mRunning{false};
  std::atomic<bool>        mCompleteFired{false};
  bool                     mFirstDataReceived = false;
  std::string              mSseBuffer;

  ChunkCallback        mOnChunk;
  ErrorCallback        mOnError;
  ConnectedCallback    mOnConnected;
  CompleteCallback     mOnComplete;
  AgentMessageCallback mOnAgentMessage;
};

} // namespace A2ui

#endif // DALI_GENERATIVE_UI_A2A_TRANSPORT_H
