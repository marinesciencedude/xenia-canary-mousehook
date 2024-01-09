#ifndef XENIA_HID_MOUSEHOOK_
#define XENIA_HID_MOUSEHOOK_

#include "xenia/hid/hookables/hookable_game.h"
#include <queue>
#include "xenia/ui/window.h"

#define XINPUT_BUTTONS_MASK 0xFFFF
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

#define VK_BIND_MWHEELUP 0x10000
#define VK_BIND_MWHEELDOWN 0x20000

namespace xe {
namespace hid {

void RegisterHookables(std::vector<std::unique_ptr<HookableGame>>& hookable_games_);
void ReadBindings(uint32_t kTitleIdDefaultBindings, std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>>& key_binds_);
void RegisterMouseListener(ui::MouseEvent& evt, std::mutex* mouse_mutex_, std::queue<MouseEvent>& mouse_events_, std::mutex* key_mutex_, bool (&key_states_)[256]);
void HandleKeyBindings(
    RawInputState& state, std::queue<MouseEvent>& mouse_events_,
    std::mutex* mouse_mutex_, std::mutex* key_mutex_, bool (&key_states_)[256],
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>>&
        key_binds_,
    uint32_t kTitleIdDefaultBindings, uint16_t* buttons, uint8_t* left_trigger,
    uint8_t* right_trigger, int16_t* thumb_lx, int16_t* thumb_ly,
    int16_t* thumb_rx, int16_t* thumb_ry, bool* modifier_pressed);

}
}

#endif