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

#include <functional>

namespace A2ui {

// --- AI-generated section starts --[20260817-101800-]-
// --- Code Writer: Cline SR
// Ported from AGenUI core/src/module/agenui_ithread.h

/**
 * @brief Thread interface
 *
 * Defines basic thread operations.
 * Implemented by MessageThread for async task processing.
 *
 * Usage:
 * @code
 * IThread* messageThread = threadManager->getMessageThread();
 * if (messageThread) {
 *     messageThread->post([=] {
 *         // Task to execute on the message thread
 *     });
 * }
 * @endcode
 */
class IThread {
public:
    virtual ~IThread() = default;

    /**
     * @brief Starts the message thread.
     * @return true if started successfully
     */
    virtual bool start() = 0;

    /**
     * @brief Stops the message thread.
     */
    virtual void stop() = 0;

    /**
     * @brief Returns whether the thread is running.
     * @return true if running
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Posts a task to the thread.
     * @param task Task function to execute
     */
    virtual void post(std::function<void()> task) = 0;

    /**
     * @brief Posts a delayed task to the thread.
     * @param task Task function to execute
     * @param delayMillis Delay in milliseconds
     */
    virtual void postDelayed(std::function<void()> task, unsigned long delayMillis) = 0;
};
// --- AI-generated section End --[20260817-101800-]-

} // namespace A2ui
