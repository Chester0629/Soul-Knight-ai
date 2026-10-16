#ifndef GAME_ENTITIES_ACTORVISUAL_HPP
#define GAME_ENTITIES_ACTORVISUAL_HPP

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/GameObject.hpp"
#include "Util/Image.hpp"

#include "data/GameData.hpp"
#include "entities/SpriteAnchor.hpp"
#include "game/ActorPose.hpp"

namespace Game {

/**
 * @class ActorVisual
 * @brief Slice 4: everything a hero / enemy / boss draws besides its entity position -- the Animator clip
 *        (idle / run / dead), the facing mirror, the ground shadow and the lock ring.
 *
 * The owner Util::GameObject (Player / Enemy / Boss) keeps the sprite drawable; this helper swaps the
 * drawable between the clips, applies SpriteAnchor with the facing, and owns two child GameObjects
 * (PTSD draws a GameObject's children with their own absolute transforms, so Sync places them):
 *   - `shadow` = the prefab child `shadow` (shadow3.png: 24x7, alpha 0.6 ellipse; layer 3 order 1);
 *   - `ring`   = the prefab child `shadow_lock` (shadow_lock.png: white 26x8 ellipse outline, layer 3
 *                order 0). It is disabled in every prefab and tinted by m_Color (heroes green
 *                (0.235, 0.914, 0.294) = RGController$$UpdateShadowLock's skill-ready colour, enemies red
 *                (0.912, 0.235, 0.235)); the game turns it on at runtime (RGEController$$BeLock on the
 *                locked target, always on the hero in the ground-truth frames).
 * Positions follow the prefab: root origin = feet = Position() - feetOffsetPx; `shadow` / `shadow_lock`
 * are children of the ROOT (not of the mirrored `img`), so a child at root-space offset o sits at
 * feet + o whatever the facing; it is scaled by kSpriteScale * the node's |lossy scale| and pivoted at
 * the sprite asset's pivot (shadow3 / shadow_lock: (0.5, 0), i.e. its bottom centre on the point).
 */
class ActorVisual {
public:
    /// Register the animation of @p clip with its per-frame pivot fractions (index-aligned frames).
    void SetClip(ActorPose::ActorClip clip, std::shared_ptr<Util::Animation> anim,
                 std::vector<std::array<float, 2>> pivots);
    /// Register the species' run clip (GameData::RunState family) and dead clip (DeadState; plays once and
    /// holds its last frame) from the sprite manifest. A species without that state keeps no such clip, so
    /// Resolve() falls back to Idle. @return true if the run clip exists.
    bool BuildStateClips(const GameData &data, const std::string &spriteId, const std::string &resourceRoot,
                         int intervalMs);
    bool HasClip(ActorPose::ActorClip clip) const { return m_Clips[Index(clip)].anim != nullptr; }
    /// The clip actually drawn for a requested @p clip: the clip itself when registered, else Idle
    /// (a species without a run / dead clip keeps idling).
    ActorPose::ActorClip Resolve(ActorPose::ActorClip clip) const {
        return HasClip(clip) ? clip : ActorPose::ActorClip::Idle;
    }

    /// Install the data anchor; its framePivots are the IDLE pivots (re-applied by SetClip).
    void SetAnchor(SpriteAnchor anchor);
    const SpriteAnchor &Anchor() const { return m_Anchor; }
    SpriteAnchor &MutableAnchor() { return m_Anchor; }

    /// Build the shadow + ring child objects from @p rig (sprites under @p resourceRoot/sprites) and
    /// attach them to @p owner. @p ringTint overrides the ring colour (default: the prefab's m_Color).
    void BuildRig(Util::GameObject &owner, const ActorRigDef &rig, const std::string &resourceRoot);

    void SetFacing(int facing) { m_Facing = facing < 0 ? -1 : 1; }
    int Facing() const { return m_Facing; }
    /// Select the clip to draw (resolved against the registered clips).
    void SetClipState(ActorPose::ActorClip clip) { m_Requested = clip; }
    ActorPose::ActorClip Requested() const { return m_Requested; }
    /// The clip really on screen after Resolve.
    ActorPose::ActorClip Shown() const { return Resolve(m_Requested); }

    /// Show / hide the lock ring (heroes: always; enemies: only the locked target).
    void SetRingVisible(bool visible);
    bool RingVisible() const { return m_RingVisible; }
    /// Re-tint the ring (RGBA 0..1); the player ring switches with the skill-ready state.
    void SetRingTint(const glm::vec4 &rgba);
    glm::vec4 RingTint() const { return m_RingTint; }
    /// Show / hide the shadow (a dead actor leaves none; its corpse carries none).
    void SetShadowVisible(bool visible);

    /// Draw-state sync for the owner at its current m_Transform.translation (= the body centre):
    /// swap the drawable to the shown clip, set the clip's pivots, apply the anchor with the facing, and
    /// place the shadow / ring.
    void Sync(Util::GameObject &owner);

    /// World position of the prefab root (feet) for an owner at @p position.
    glm::vec2 FeetOf(glm::vec2 position) const { return position - m_Anchor.feetOffsetPx; }
    /// Where a shadow / ring node of @p node is placed (the centre-pivot translation, before the
    /// pivot shift) for an owner at @p position -- exposed so the placement is unit-tested.
    glm::vec2 RigNodeOrigin(const RigNodeDef &node, glm::vec2 position) const;

    const std::shared_ptr<Util::GameObject> &ShadowObject() const { return m_Shadow; }
    const std::shared_ptr<Util::GameObject> &RingObject() const { return m_Ring; }

private:
    struct Clip {
        std::shared_ptr<Util::Animation> anim;
        std::vector<std::array<float, 2>> pivots;
    };
    static std::size_t Index(ActorPose::ActorClip c) { return static_cast<std::size_t>(c); }
    static std::shared_ptr<Util::GameObject> MakeNode(const RigNodeDef &node, const std::string &resourceRoot,
                                                      float z, std::shared_ptr<Util::Image> &outImage);
    void PlaceNode(Util::GameObject &obj, const RigNodeDef &node, const Util::Image &image,
                   glm::vec2 position) const;

    SpriteAnchor m_Anchor;
    std::array<Clip, 3> m_Clips;
    ActorPose::ActorClip m_Requested = ActorPose::ActorClip::Idle;
    ActorPose::ActorClip m_Drawn = ActorPose::ActorClip::Idle;
    bool m_DrawnValid = false;
    int m_Facing = 1;
    RigNodeDef m_ShadowDef;
    RigNodeDef m_RingDef;
    std::shared_ptr<Util::Image> m_ShadowImage;
    std::shared_ptr<Util::Image> m_RingImage;
    std::shared_ptr<Util::GameObject> m_Shadow;
    std::shared_ptr<Util::GameObject> m_Ring;
    glm::vec4 m_RingTint{1.0F, 1.0F, 1.0F, 1.0F};
    bool m_RingVisible = false;
    bool m_ShadowVisible = true;
};

} // namespace Game

#endif /* GAME_ENTITIES_ACTORVISUAL_HPP */
