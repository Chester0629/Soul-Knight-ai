#include "Core/SceneManager.hpp"

#include <utility>

namespace Core {

void SceneManager::DoPush(const std::shared_ptr<Scene> &scene, bool pauseBelow) {
    if (scene == nullptr) {
        return;
    }
    if (pauseBelow && !m_Stack.empty()) {
        m_Stack.back()->OnPause();
    }
    m_Stack.push_back(scene);
    scene->OnEnter();
}

void SceneManager::DoPop(bool resumeBelow) {
    if (m_Stack.empty()) {
        return;
    }
    auto top = m_Stack.back();
    m_Stack.pop_back();
    top->OnExit();
    if (resumeBelow && !m_Stack.empty()) {
        m_Stack.back()->OnResume();
    }
}

void SceneManager::Push(const std::shared_ptr<Scene> &scene) {
    if (m_Updating) {
        m_Pending.push_back({OpType::Push, scene});
        return;
    }
    DoPush(scene, true);
}

void SceneManager::Pop() {
    if (m_Updating) {
        m_Pending.push_back({OpType::Pop, nullptr});
        return;
    }
    DoPop(true);
}

void SceneManager::Replace(const std::shared_ptr<Scene> &scene) {
    if (m_Updating) {
        m_Pending.push_back({OpType::Replace, scene});
        return;
    }
    DoPop(false);
    DoPush(scene, false);
}

void SceneManager::Clear() {
    if (m_Updating) {
        m_Pending.push_back({OpType::Clear, nullptr});
        return;
    }
    while (!m_Stack.empty()) {
        DoPop(false);
    }
}

void SceneManager::Update(float dtMs) {
    auto top = Current();
    m_Updating = true;
    if (top != nullptr) {
        top->Update(dtMs);
    }
    m_Updating = false;

    if (m_Pending.empty()) {
        return;
    }
    auto pending = std::move(m_Pending);
    m_Pending.clear();
    for (const auto &op : pending) {
        switch (op.type) {
        case OpType::Push:
            DoPush(op.scene, true);
            break;
        case OpType::Pop:
            DoPop(true);
            break;
        case OpType::Replace:
            DoPop(false);
            DoPush(op.scene, false);
            break;
        case OpType::Clear:
            while (!m_Stack.empty()) {
                DoPop(false);
            }
            break;
        }
    }
}

void SceneManager::Render() {
    if (!m_Stack.empty()) {
        m_Stack.back()->Render();
    }
}

std::shared_ptr<Scene> SceneManager::Current() const {
    if (m_Stack.empty()) {
        return nullptr;
    }
    return m_Stack.back();
}

std::size_t SceneManager::Count() const { return m_Stack.size(); }

bool SceneManager::Empty() const { return m_Stack.empty(); }

} // namespace Core
