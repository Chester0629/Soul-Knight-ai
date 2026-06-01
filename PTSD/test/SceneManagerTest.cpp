#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Core/Scene.hpp"
#include "Core/SceneManager.hpp"

using Core::Scene;
using Core::SceneManager;

namespace {

// A scene that records every lifecycle callback into a shared log.
class TestScene : public Scene {
public:
    TestScene(std::string name, std::vector<std::string> *log)
        : m_Name(std::move(name)),
          m_Log(log) {}

    void OnEnter() override { Record("enter"); }
    void OnExit() override { Record("exit"); }
    void OnPause() override { Record("pause"); }
    void OnResume() override { Record("resume"); }
    void Update(float /*dtMs*/) override {
        ++updates;
        Record("update");
    }
    void Render() override { Record("render"); }

    int updates = 0;

private:
    void Record(const std::string &ev) {
        if (m_Log != nullptr) {
            m_Log->push_back(m_Name + ":" + ev);
        }
    }

    std::string m_Name;
    std::vector<std::string> *m_Log;
};

} // namespace

TEST(SceneManagerTest, PushEntersAndBecomesCurrent) {
    SceneManager mgr;
    std::vector<std::string> log;
    auto a = std::make_shared<TestScene>("A", &log);
    mgr.Push(a);

    EXPECT_EQ(mgr.Count(), 1u);
    EXPECT_EQ(mgr.Current(), a);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "A:enter");
}

TEST(SceneManagerTest, PushPausesBelowPopResumes) {
    SceneManager mgr;
    std::vector<std::string> log;
    auto a = std::make_shared<TestScene>("A", &log);
    auto b = std::make_shared<TestScene>("B", &log);

    mgr.Push(a);
    mgr.Push(b); // A pauses, B enters
    EXPECT_EQ(mgr.Current(), b);
    EXPECT_EQ(mgr.Count(), 2u);

    mgr.Pop(); // B exits, A resumes
    EXPECT_EQ(mgr.Current(), a);

    const std::vector<std::string> expected{"A:enter", "A:pause", "B:enter",
                                            "B:exit", "A:resume"};
    EXPECT_EQ(log, expected);
}

TEST(SceneManagerTest, ReplaceSwapsTopWithoutTouchingBelow) {
    SceneManager mgr;
    std::vector<std::string> log;
    auto a = std::make_shared<TestScene>("A", &log);
    auto b = std::make_shared<TestScene>("B", &log);

    mgr.Push(a);
    log.clear();
    mgr.Replace(b); // A exits, B enters; no pause/resume churn

    EXPECT_EQ(mgr.Current(), b);
    EXPECT_EQ(mgr.Count(), 1u);
    const std::vector<std::string> expected{"A:exit", "B:enter"};
    EXPECT_EQ(log, expected);
}

TEST(SceneManagerTest, UpdateRoutesToTopOnly) {
    SceneManager mgr;
    std::vector<std::string> log;
    auto a = std::make_shared<TestScene>("A", &log);
    auto b = std::make_shared<TestScene>("B", &log);

    mgr.Push(a);
    mgr.Push(b);
    mgr.Update(16.0F);

    EXPECT_EQ(b->updates, 1);
    EXPECT_EQ(a->updates, 0);
}

TEST(SceneManagerTest, RenderRoutesToTopOnly) {
    SceneManager mgr;
    std::vector<std::string> log;
    auto a = std::make_shared<TestScene>("A", &log);
    mgr.Push(a);
    log.clear();

    mgr.Render();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "A:render");
}

TEST(SceneManagerTest, ClearExitsAllTopFirst) {
    SceneManager mgr;
    std::vector<std::string> log;
    mgr.Push(std::make_shared<TestScene>("A", &log));
    mgr.Push(std::make_shared<TestScene>("B", &log));
    log.clear();

    mgr.Clear();
    EXPECT_TRUE(mgr.Empty());
    const std::vector<std::string> expected{"B:exit", "A:exit"};
    EXPECT_EQ(log, expected);
}

TEST(SceneManagerTest, TransitionDuringUpdateIsDeferred) {
    // A scene that pushes another during its own Update must not corrupt the
    // stack; the push is applied after Update returns.
    struct PushingScene : Scene {
        SceneManager *mgr = nullptr;
        std::shared_ptr<Scene> next;
        bool done = false;
        void Update(float /*dtMs*/) override {
            if (!done) {
                done = true;
                mgr->Push(next);
            }
        }
    };

    SceneManager mgr;
    std::vector<std::string> log;
    auto pusher = std::make_shared<PushingScene>();
    auto next = std::make_shared<TestScene>("N", &log);
    pusher->mgr = &mgr;
    pusher->next = next;

    mgr.Push(pusher);
    mgr.Update(16.0F); // pusher.Update defers the push; applied afterward

    EXPECT_EQ(mgr.Count(), 2u);
    EXPECT_EQ(mgr.Current(), next);
}

TEST(SceneManagerTest, EmptyManagerSafeNoOps) {
    SceneManager mgr;
    EXPECT_TRUE(mgr.Empty());
    EXPECT_EQ(mgr.Current(), nullptr);

    mgr.Update(16.0F); // no crash
    mgr.Render();      // no crash
    mgr.Pop();         // no crash
    EXPECT_TRUE(mgr.Empty());
}
