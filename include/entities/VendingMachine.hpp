#ifndef GAME_VENDING_MACHINE_HPP
#define GAME_VENDING_MACHINE_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/GameObject.hpp"
#include "Util/Image.hpp"
#include "Util/Text.hpp"

namespace Game {

/**
 * @class VendingMachine
 * @brief A shop-room machine -- the port of rgprefab/room/event/vending_machine_i
 *        (ObjectVendingMachine, item_value 10, object_list = the four pots),
 *        vending_machine_w (item_value 12, a sample weapon from the chest table)
 *        and water_dispenser (ItemWaterDispenser, item_value 0).
 *
 * The machine shows its stock on the display frame (objects3_13 / 14) with the
 * price; the player stands in its 2x2 trigger and presses the use key to pay
 * (RGGameProcess.coin_value) and take the item. One sale per machine.
 */
class VendingMachine : public Util::GameObject {
public:
    enum class Kind { Item, Weapon, Water };

    VendingMachine(const std::string &resourceRoot, glm::vec2 pos, Kind kind, int price,
                   const std::string &bodySprite, const std::string &displaySprite,
                   const std::string &stockId, const std::string &stockSprite);

    Kind GetKind() const { return m_Kind; }
    int Price() const { return m_Price; }
    const std::string &Stock() const { return m_Stock; }
    bool Sold() const { return m_Sold; }
    void MarkSold();

    glm::vec2 Position() const { return m_Transform.translation; }
    /// Extra GameObjects the scene must add to its renderer (display + icon + price).
    const std::shared_ptr<Util::GameObject> &Display() const { return m_Display; }
    const std::shared_ptr<Util::GameObject> &Icon() const { return m_Icon; }
    const std::shared_ptr<Util::GameObject> &PriceTag() const { return m_PriceTag; }

private:
    Kind m_Kind;
    int m_Price;
    std::string m_Stock;
    bool m_Sold = false;
    std::shared_ptr<Util::GameObject> m_Display;
    std::shared_ptr<Util::GameObject> m_Icon;
    std::shared_ptr<Util::GameObject> m_PriceTag;
    std::shared_ptr<Util::Text> m_PriceText;
};

} // namespace Game

#endif /* GAME_VENDING_MACHINE_HPP */
