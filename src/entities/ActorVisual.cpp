#include "entities/ActorVisual.hpp"

#include <utility>

#include "game/RenderLayout.hpp"
#include "game/WorldScale.hpp"

namespace Game {

void ActorVisual::SetClip(ActorPose::ActorClip clip, std::shared_ptr<Util::Animation> anim,
                          std::vector<std::array<float, 2>> pivots) {
    Clip &c = m_Clips[Index(clip)];
    c.anim = std::move(anim);
    c.pivots = std::move(pivots);
    m_DrawnValid = false; // re-bind the drawable on the next Sync
}

bool ActorVisual::BuildStateClips(const GameData &data, const std::string &spriteId,
                                  const std::string &resourceRoot, int intervalMs) {
    const std::size_t interval = static_cast<std::size_t>(intervalMs > 0 ? intervalMs : 1);
    const std::string run = data.RunState(spriteId);
    bool hasRun = false;
    if (!run.empty()) {
        std::vector<std::string> frames = data.SpriteFrames(spriteId, resourceRoot, run);
        if (!frames.empty()) {
            SetClip(ActorPose::ActorClip::Run, std::make_shared<Util::Animation>(frames, true, interval, true),
                    data.SpriteFramePivots(spriteId, run));
            hasRun = true;
        }
    }
    const std::string dead = data.DeadState(spriteId);
    if (!dead.empty()) {
        std::vector<std::string> frames = data.SpriteFrames(spriteId, resourceRoot, dead);
        if (!frames.empty()) {
            SetClip(ActorPose::ActorClip::Dead, std::make_shared<Util::Animation>(frames, true, interval, false),
                    data.SpriteFramePivots(spriteId, dead));
        }
    }
    return hasRun;
}

void ActorVisual::SetAnchor(SpriteAnchor anchor) {
    m_Anchor = std::move(anchor);
    // The anchor's framePivots are the pivots of the default (idle) state.
    m_Clips[Index(ActorPose::ActorClip::Idle)].pivots = m_Anchor.framePivots;
    m_DrawnValid = false;
}

glm::vec2 ActorVisual::RigNodeOrigin(const RigNodeDef &node, glm::vec2 position) const {
    return FeetOf(position) + glm::vec2{Scale::UnitsToPx(node.offsetX), Scale::UnitsToPx(node.offsetY)};
}

std::shared_ptr<Util::GameObject> ActorVisual::MakeNode(const RigNodeDef &node, const std::string &resourceRoot,
                                                        float z, std::shared_ptr<Util::Image> &outImage) {
    // Point-filtered: a 1 px outline / hard ellipse edge must stay crisp (Util::Image(path, false)).
    outImage = std::make_shared<Util::Image>(resourceRoot + "/sprites/" + node.sprite, /*useAA=*/false);
    outImage->SetTint({node.color[0], node.color[1], node.color[2], node.color[3]}); // m_Color
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(outImage);
    obj->SetZIndex(z);
    return obj;
}

void ActorVisual::BuildRig(Util::GameObject &owner, const ActorRigDef &rig, const std::string &resourceRoot) {
    m_ShadowDef = rig.shadow;
    m_RingDef = rig.ring;
    if (m_ShadowDef.present && m_ShadowDef.enabled) {
        m_Shadow = MakeNode(m_ShadowDef, resourceRoot,
                            RenderLayout::SortZ(m_ShadowDef.sortLayer, m_ShadowDef.sortOrder), m_ShadowImage);
        m_Shadow->SetVisible(m_ShadowVisible);
        owner.AddChild(m_Shadow);
    }
    if (m_RingDef.present) {
        m_RingTint = {m_RingDef.color[0], m_RingDef.color[1], m_RingDef.color[2], m_RingDef.color[3]};
        m_Ring = MakeNode(m_RingDef, resourceRoot, RenderLayout::SortZ(m_RingDef.sortLayer, m_RingDef.sortOrder),
                          m_RingImage);
        m_Ring->SetVisible(m_RingVisible);
        owner.AddChild(m_Ring);
    }
}

void ActorVisual::SetRingVisible(bool visible) {
    m_RingVisible = visible;
    if (m_Ring != nullptr) {
        m_Ring->SetVisible(visible);
    }
}

void ActorVisual::SetRingTint(const glm::vec4 &rgba) {
    m_RingTint = rgba;
    if (m_RingImage != nullptr) {
        m_RingImage->SetTint(rgba);
    }
}

void ActorVisual::SetShadowVisible(bool visible) {
    m_ShadowVisible = visible;
    if (m_Shadow != nullptr) {
        m_Shadow->SetVisible(visible);
    }
}

void ActorVisual::PlaceNode(Util::GameObject &obj, const RigNodeDef &node, const Util::Image &image,
                            glm::vec2 position) const {
    const float s = Scale::kSpriteScale;
    obj.m_Transform.scale = {s * node.scaleX, s * node.scaleY};
    obj.m_Transform.translation = RigNodeOrigin(node, position);
    // The node's own sprite pivot (source px from the rect centre) lands on the translation.
    const glm::vec2 size = image.GetSize();
    obj.SetPivot({(node.pivotX - 0.5F) * size.x, (node.pivotY - 0.5F) * size.y});
}

void ActorVisual::Sync(Util::GameObject &owner) {
    const ActorPose::ActorClip shown = Shown();
    Clip &c = m_Clips[Index(shown)];
    if (c.anim != nullptr) {
        if (!m_DrawnValid || m_Drawn != shown) {
            owner.SetDrawable(c.anim);
            m_Anchor.framePivots = c.pivots;
            m_Drawn = shown;
            m_DrawnValid = true;
        }
        m_Anchor.Apply(owner, *c.anim, m_Facing);
    }
    const glm::vec2 position = owner.m_Transform.translation;
    if (m_Shadow != nullptr) {
        PlaceNode(*m_Shadow, m_ShadowDef, *m_ShadowImage, position);
    }
    if (m_Ring != nullptr) {
        PlaceNode(*m_Ring, m_RingDef, *m_RingImage, position);
    }
}

} // namespace Game
