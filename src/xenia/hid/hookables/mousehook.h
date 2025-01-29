#ifndef XENIA_HID_MOUSEHOOK_H_
#define XENIA_HID_MOUSEHOOK_H_

#include <queue>

#include "xenia/hid/hookables/hookable_game.h"
#include "xenia/ui/window.h"

#define VK_BIND_MWHEELUP 0x0E
#define VK_BIND_MWHEELDOWN 0x0F

#define XINPUT_BUTTONS_MASK 0xFFFF

#define XINPUT_BIND_UP X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_UP
#define XINPUT_BIND_DOWN X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_DOWN
#define XINPUT_BIND_LEFT X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_LEFT
#define XINPUT_BIND_RIGHT X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_RIGHT

#define XINPUT_BIND_START X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_START
#define XINPUT_BIND_BACK X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_BACK

#define XINPUT_BIND_LS X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_LEFT_THUMB
#define XINPUT_BIND_RS X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_RIGHT_THUMB

#define XINPUT_BIND_LB X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_LEFT_SHOULDER
#define XINPUT_BIND_RB X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_RIGHT_SHOULDER

#define XINPUT_BIND_A X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_A
#define XINPUT_BIND_B X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_B
#define XINPUT_BIND_X X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_X
#define XINPUT_BIND_Y X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_Y

#define XINPUT_BIND_LEFT_TRIGGER (1 << 16)
#define XINPUT_BIND_RIGHT_TRIGGER (1 << 17)

#define XINPUT_BIND_LS_UP (1 << 18)
#define XINPUT_BIND_LS_DOWN (1 << 19)
#define XINPUT_BIND_LS_LEFT (1 << 20)
#define XINPUT_BIND_LS_RIGHT (1 << 21)

#define XINPUT_BIND_RS_UP (1 << 22)
#define XINPUT_BIND_RS_DOWN (1 << 23)
#define XINPUT_BIND_RS_LEFT (1 << 24)
#define XINPUT_BIND_RS_RIGHT (1 << 25)

#define XINPUT_BIND_MODIFIER (1 << 26)

#define XINPUT_BIND_WEAPON1 ((uint64_t)1 << 54)
#define XINPUT_BIND_WEAPON2 ((uint64_t)1 << 55)
#define XINPUT_BIND_WEAPON3 ((uint64_t)1 << 56)
#define XINPUT_BIND_WEAPON4 ((uint64_t)1 << 57)
#define XINPUT_BIND_WEAPON5 ((uint64_t)1 << 58)
#define XINPUT_BIND_WEAPON6 ((uint64_t)1 << 59)
#define XINPUT_BIND_WEAPON7 ((uint64_t)1 << 60)
#define XINPUT_BIND_WEAPON8 ((uint64_t)1 << 61)
#define XINPUT_BIND_WEAPON9 ((uint64_t)1 << 62)
#define XINPUT_BIND_WEAPON10 ((uint64_t)1 << 63)

namespace xe {
namespace hid {

void RegisterHookables(
    std::vector<std::unique_ptr<HookableGame>>& hookable_games_);
void ParseCustomKeyBinding(
    std::map<uint32_t,
             std::map<std::string, std::map<ui::VirtualKey, uint64_t>>>&
        key_binds_);
void OnMouse(ui::MouseEvent& evt, std::queue<MouseEvent>& mouse_events_,
             uint8_t (&key_states_)[256]);
void HandleKeyBindings(
    RawInputState& state, std::queue<MouseEvent>& mouse_events_,
    uint8_t (&key_states_)[256],
    std::map<uint32_t,
             std::map<std::string, std::map<ui::VirtualKey, uint64_t>>>&
        key_binds_,
    uint32_t title_id,
    std::vector<std::unique_ptr<HookableGame>>& hookable_games_,
    uint16_t* buttons, uint8_t* left_trigger, uint8_t* right_trigger,
    int16_t* thumb_lx, int16_t* thumb_ly, int16_t* thumb_rx, int16_t* thumb_ry,
    bool* modifier_pressed, bool* weapon_switch, int* weapon);

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_MOUSEHOOK_H_