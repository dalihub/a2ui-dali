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

#include "thread-manager.h"
#include "message-thread.h"
#include <cstdio>

namespace A2ui {

// Ported from AGenUI core/src/module/agenui_thread_manager.cpp

ThreadManager& ThreadManager::getInstance() {
    static ThreadManager instance;
    return instance;
}

ThreadManager::~ThreadManager() {
    for (auto& pair : _threads) {
        pair.second->stop();
        delete pair.second;
        pair.second = nullptr;
    }
    _threads.clear();
}

bool ThreadManager::createThread(int threadId) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_threads.find(threadId) != _threads.end()) {
        fprintf(stderr, "[A2ui] %d already exists\n", threadId);
        return true;
    }

    std::string name = "A2ui-" + std::to_string(threadId);
    // Order: create → start → insert into map
    IThread *newThread = new MessageThread(name);
    newThread->start();
    _threads[threadId] = newThread;
    fprintf(stderr, "[A2ui] created thread '%s'\n", name.c_str());
    return true;
}

void ThreadManager::destroyThread(int threadId) {
    fprintf(stderr, "[A2ui] begin destroying thread for %d\n", threadId);
    IThread* thread = nullptr;
    // Order: remove → stop → delete (exact reverse of createThread)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _threads.find(threadId);
        if (it == _threads.end()) {
            return;
        }
        thread = it->second;
        _threads.erase(it);
    }
    thread->stop();
    delete thread;
    thread = nullptr;

    fprintf(stderr, "[A2ui] destroyed %d\n", threadId);
}

IThread* ThreadManager::getMessageThread(int threadId) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _threads.find(threadId);
    if (it == _threads.end()) {
        return nullptr;
    }
    return it->second;
}

} // namespace A2ui
