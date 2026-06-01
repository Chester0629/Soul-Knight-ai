#ifndef CORE_SCENE_HPP
#define CORE_SCENE_HPP

namespace Core {
/**
 * @class Scene
 * @brief A discrete game screen (title, hero-select, gameplay, pause, ...).
 *
 * Scenes are driven by Core::SceneManager, which keeps them on a stack. The
 * lifecycle hooks let a scene react to stack transitions:
 *
 * - @ref OnEnter  : the scene was pushed onto / placed on the stack.
 * - @ref OnExit   : the scene was popped / replaced and is being discarded.
 * - @ref OnPause  : another scene was pushed on top (this one stays alive).
 * - @ref OnResume : the scene above was popped and this one is active again.
 *
 * Each frame the manager calls @ref Update then @ref Render on the top scene.
 *
 * @note This is an abstract base: inherit and override the hooks you need. All
 * hooks have empty default implementations, so a minimal scene only needs to
 * override @ref Update and/or @ref Render.
 */
class Scene {
public:
    virtual ~Scene() = default;

    /// Called when the scene is pushed onto the stack.
    virtual void OnEnter() {}

    /// Called when the scene is popped/replaced and discarded.
    virtual void OnExit() {}

    /// Called when another scene is pushed on top of this one.
    virtual void OnPause() {}

    /// Called when the scene above is popped and this one becomes active again.
    virtual void OnResume() {}

    /**
     * @brief Advance the scene's simulation.
     * @param dtMs Elapsed time for this step, in milliseconds.
     */
    virtual void Update(float /*dtMs*/) {}

    /// Draw the scene.
    virtual void Render() {}
};
} // namespace Core

#endif /* CORE_SCENE_HPP */
