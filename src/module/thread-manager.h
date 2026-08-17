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

#include "ithread.h"
#include <mutex>
#include <unordered_map>

namespace A2ui {

// --- AI-generated section starts --[20260817-101800-]-
// --- Code Writer: Cline SR
// Ported from AGenUI core/src/module/agenui_thread_manager.h

/**
 * @brief Thread manager (singleton)
 *
 * Manages worker threads keyed by threadId.
 * The map-based storage is designed to be extensible, allowing multiple independent
 * threads to be registered (e.g. one dedicated thread per engine instance).
 *
 * Thread safety:
 * - Thread map is protected by a mutex
 * - Thread creation and destruction are managed centrally
 */
class ThreadManager {
public:
    /**
     * @brief Returns the singleton instance.
     */
    static ThreadManager& getInstance();

    /**
     * @brief Creates a worker thread for the given threadId.
     * @param threadId
     * @return true if successful
     */
    bool createThread(int threadId);

    /**
     * @brief Destroys the worker thread for the given threadId.
     * @param threadId
     */
    void destroyThread(int threadId);

    /**
     * @brief Returns the message thread for the given threadId.
     * @param threadId
     * @return Thread pointer, or nullptr if not started
     */
    IThread* getMessageThread(int threadId);

private:
    ThreadManager() = default;
    ~ThreadManager();
    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;

    std::unordered_map<int, IThread*> _threads;
    std::mutex _mutex;
};
// --- AI-generated section End --[20260817-101800-]-

} // namespace A2ui
