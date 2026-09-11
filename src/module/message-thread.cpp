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

#include "message-thread.h"
#include <cstdio>
#include <chrono>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__) || defined(__ANDROID__) || defined(HARMONY)
#include <pthread.h>
#endif

namespace A2ui {

// --- AI-generated section starts --[20260817-101800-]-
// --- Code Writer: Cline SR
// Ported from AGenUI core/src/module/agenui_message_thread.cpp

MessageThread::MessageThread(const std::string& name) : _name(name), _isRunning(false), _shouldStop(false) {
}

MessageThread::~MessageThread() {
}

bool MessageThread::start() {
    if (_isRunning) {
        fprintf(stderr, "[A2ui] MessageThread already running\n");
        return true;
    }

    _shouldStop = false;
    _isRunning = true;

    // Start the worker thread
    _workerThread = std::thread(&MessageThread::workerThreadLoop, this);
    _threadId = _workerThread.get_id();

    fprintf(stderr, "[A2ui] started, thread_id: %zu\n", std::hash<std::thread::id>{}(_threadId));
    return true;
}

void MessageThread::stop() {
    if (!_isRunning) {
        return;
    }

    // Signal stop
    _shouldStop = true;
    _isRunning = false;

    // Wake up the worker thread
    _condition.notify_one();

    // Drain the task queues
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        while (!_taskQueue.empty()) {
            _taskQueue.pop();
        }
        while (!_delayedTaskQueue.empty()) {
            _delayedTaskQueue.pop();
        }
    }

    // Wait for the worker thread to exit
    if (_workerThread.joinable()) {
        _workerThread.join();
    }

    fprintf(stderr, "[A2ui] stopped\n");
}

void MessageThread::post(std::function<void()> task) {
    if (!_isRunning) {
        fprintf(stderr, "[A2ui] MessageThread not running, task ignored\n");
        return;
    }

    if (!task) {
        return;
    }

    // Enqueue the task
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _taskQueue.push(task);
    }

    // Wake up the worker thread
    _condition.notify_one();
}

void MessageThread::postDelayed(std::function<void()> task, unsigned long delayMillis) {
    if (!_isRunning) {
        fprintf(stderr, "[A2ui] MessageThread not running, delayed task ignored\n");
        return;
    }

    if (!task) {
        return;
    }

    // Compute execution time
    auto executeTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMillis);

    // Enqueue the delayed task
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _delayedTaskQueue.push({task, executeTime});
    }

    // Wake up the worker thread
    _condition.notify_one();
}

bool MessageThread::isRunning() const {
    return _isRunning;
}

std::thread::id MessageThread::getThreadId() const {
    return _threadId;
}

void MessageThread::workerThreadLoop() {
    // Set thread name for debugger identification
#if defined(__APPLE__)
    pthread_setname_np(_name.c_str());
#elif defined(__linux__) || defined(__ANDROID__) || defined(HARMONY)
    pthread_setname_np(pthread_self(), _name.c_str());
#endif

    fprintf(stderr, "[A2ui] [%s] worker loop started\n", _name.c_str());

    while (!_shouldStop) {
        std::function<void()> task;
        bool hasTask = false;

        // Dequeue a task
        {
            std::unique_lock<std::mutex> lock(_queueMutex);

            // Promote expired delayed tasks
            processDelayedTasks();

            // Compute wait duration
            std::chrono::milliseconds waitTime(100); // default 100ms
            if (!_delayedTaskQueue.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto nextTaskTime = _delayedTaskQueue.top().executeTime;
                if (nextTaskTime > now) {
                    waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(nextTaskTime - now);
                }
            }

            // Wait for a task or stop signal
            if (_taskQueue.empty() && !_shouldStop) {
                _condition.wait_for(lock, waitTime, [this] {
                    return !_taskQueue.empty() || _shouldStop;
                });
            }

            // Exit if stopped and all queues are empty
            if (_shouldStop && _taskQueue.empty() && _delayedTaskQueue.empty()) {
                break;
            }

            // Dequeue next task
            if (!_taskQueue.empty()) {
                task = _taskQueue.front();
                _taskQueue.pop();
                hasTask = true;
            }
        }

        // Execute
        if (hasTask && task) {
            task();
        }
    }

    fprintf(stderr, "[A2ui] [%s] worker loop stopped\n", _name.c_str());
}

void MessageThread::processDelayedTasks() {
    auto now = std::chrono::steady_clock::now();

    // Move expired delayed tasks to the main queue
    while (!_delayedTaskQueue.empty()) {
        const auto& delayedTask = _delayedTaskQueue.top();
        if (delayedTask.executeTime <= now) {
            _taskQueue.push(delayedTask.task);
            _delayedTaskQueue.pop();
        } else {
            break;
        }
    }
}
// --- AI-generated section End --[20260817-101800-]-

} // namespace A2ui
