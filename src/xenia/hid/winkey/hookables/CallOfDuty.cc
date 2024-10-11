/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/CallOfDuty.h"

#include "xenia/base/platform_win.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

using namespace xe::kernel;

DECLARE_double(sensitivity);
DECLARE_double(fov_sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);
DECLARE_bool(d3d12_readback_resolve);

const uint32_t kTitleIdCODAW = 0x41560914;
const uint32_t kTitleIdCODGhostsDEV = 0x4156088E;
const uint32_t kTitleIdCODNX1 = 0x4156089E;
const uint32_t kTitleIdCODBO2 = 0x415608C3;
const uint32_t kTitleIdCODMW3 = 0x415608CB;
const uint32_t kTitleIdCODMW2 = 0x41560817;
const uint32_t kTitleIdCODWaW = 0x4156081C;
const uint32_t kTitleIdCOD4 = 0x415607E6;
const uint32_t kTitleIdCOD3 = 0x415607E1;
const uint32_t kTitleIdWolfenstein = 0x415607DE;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t cg_fov_address;
  uint32_t cg_fov;  // cg_fo
  uint32_t title_id;
  uint32_t x_address;
  uint32_t y_address;
  uint32_t fovscale_address;
  uint32_t base_address;  // Static addresses in older cods, needs pointers in
                          // newer cods?
  uint32_t Dvar_GetBool_address;
};

std::map<CallOfDutyGame::GameBuild, GameBuildAddrs> supported_builds{
    {CallOfDutyGame::GameBuild::CallOfDuty4_SP,
     {0x82044468, 0x63675F66, kTitleIdCOD4, 0x824F6BDC, 0x824f6bd8, 0x824F6BC8,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_TU0_MP,
     {0x82050E18, 0x63675F66, kTitleIdCOD4, 0x25530, NULL, 0x823B53A8,
      0x82435818, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_TU4_MP,
     {0x82051048, 0x63675F66, kTitleIdCOD4, 0x25534, NULL, 0x84CD7D44,
      0x82435AB8, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_253SP,
     {0x8204EB24, 0x63675F66, kTitleIdCOD4, 0x8261246C, 0x82612468, 0x82612458,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_253SP_exe,
     {0x8200EAA4, 0x63675F66, kTitleIdCOD4, 0x8275BC4C, 0x8275BC48, 0x826220CC,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_253MP,
     {0x82055EF4, 0x63675F66, kTitleIdCOD4, 0x82B859B8, 0x82B859B4, 0x8254EE50,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_253MP_exe,
     {0x82011EF4, 0x63675F66, kTitleIdCOD4, 0x82E4BF30, 0x82E4BF2C, 0x82784414,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_270SP,
     {0x8204E7FC, 0x63675F66, kTitleIdCOD4, 0x8262E168, 0x8262E164, 0x82612458,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_270SP_exe,
     {0x8200E4FC, 0x63675F66, kTitleIdCOD4, 0x82777A8C, 0x82777A5C, 0x825F358C,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_270MP,
     {0x8205617C, 0x63675F66, kTitleIdCOD4, 0x82B9F664, 0x82B9F660, 0x82558944,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_270MP_exe,
     {0x82012114, 0x63675F66, kTitleIdCOD4, 0x82E75CD4, 0x82E75CD0, 0x8279440C,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_290SP,
     {0x8203ABE8, 0x63675F66, kTitleIdCOD4, 0x8247C808, 0x8247C804, 0x82348900,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_290SP_exe,
     {0x8200E9EC, 0x63675F66, kTitleIdCOD4, 0x825D6C60, 0x825D6C5C, 0x8245240C,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_290MP,
     {0x82042588, 0x63675F66, kTitleIdCOD4, 0x82A7F57C, 0x82A7F578, 0x823A1F04,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_290MP_exe,
     {0x82012624, 0x63675F66, kTitleIdCOD4, 0x82F07B64, 0x82F07B60, 0x827A480C,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_328SP,
     {0x82009C80, 0x63675F66, kTitleIdCOD4, 0x826A8640, 0x826A863C, 0x82567E8C,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_328SP_exe,
     {0x8200EB58, 0x63675F66, kTitleIdCOD4, 0x82822D00, 0x82822CFC, 0x82683F68,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_328MP,
     {0x8200BB2C, 0x63675F66, kTitleIdCOD4, 0x25278, NULL, 0x826027D0,
      0x82B959C8, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty4_Alpha_328MP_exe,
     {0x82012664, 0x63675F66, kTitleIdCOD4, 0x25278, NULL, 0x82794968,
      0x82DED60C, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyMW2_Alpha_482SP,
     {0x82007560, 0x63675F66, kTitleIdCODMW2, 0x82627D08, 0x82627D04,
      0x824609CC, NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyMW2_Alpha_482MP,
     {0x8200FF48, 0x63675F66, kTitleIdCODMW2, 0x335C, NULL, 0x83A25CBC,
      0x8255DA70, 0x82303b00}},
    {CallOfDutyGame::GameBuild::CallOfDutyMW2_TU0_SP,
     {0x82020954, 0x63675F66, kTitleIdCODMW2, 0x82648B60, 0x82648B5C,
      0x82470AE0, NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty3_SP,
     {0x82078F00, 0x63675F66, kTitleIdCOD3, 0x82A58F68, 0x82A58F64, 0x825CE5F8,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty3_MP_TU0,
     {0x82078614, 0x63675F66, kTitleIdCOD3, 0x82C2F378, 0x82C2F374, 0x82C2F350,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDuty3_MP_TU3,
     {0x8206E994, 0x63675F66, kTitleIdCOD3, 0x82BEF278, 0x82BEF274, 0x82BEF250,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::New_Moon_PatchedXEX,
     {0x82004860, 0x63675F66, kTitleIdCODBO2, 0x2C38, NULL, 0x82866DAC,
      0x829FA9C8, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyMW3_TU0_MP,
     {0x8200C558, 0x63675F66, kTitleIdCODMW3, 0x35F4, NULL, 0x82599598,
      0x826E0A80, 0x823243E0}},
    {CallOfDutyGame::GameBuild::CallOfDutyMW2_TU0_MP,
     {0x820102D8, 0x63675F66, kTitleIdCODMW2, 0x335C, NULL, 0x83AE320C,
      0x825A3FAC, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyNX1_Nightly_SP_maps,
     {0x82021104, 0x63675F66, kTitleIdCODNX1, 0x82807130, 0x8280712C,
      0x825EC774, NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyNX1_nx1sp,
     {0x8200FC1C, 0x63675F66, kTitleIdCODNX1, 0x82AF11B8, 0x82AF11B4,
      0x828B8654, NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyNX1_nx1mp_demo,
     {0x82012228, 0x63675F66, kTitleIdCODNX1, 0x3668, NULL, 0x84136E78,
      0x827519D4, 0x823748E0}},
    {CallOfDutyGame::GameBuild::CallOfDutyNX1_nx1mp,
     {0x8201E584, 0x63675F66, kTitleIdCODNX1, 0x3668, NULL, 0x83D66260,
      0x82B79CD0, 0x82556C08}},
    {CallOfDutyGame::GameBuild::CallOfDutyNX1_NightlyMPmaps,
     {0x8201DD04, 0x63675F66, kTitleIdCODNX1, 0x3668, NULL, 0x83D060E0,
      0x82B19C50, 0x82531558}},
    {CallOfDutyGame::GameBuild::CallOfDutyWaW_TU7_SP,
     {0x82055874, 0x63675F66, kTitleIdCODWaW, 0xEAEC, NULL, 0x824DE870,
      0x849355D4, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyWaW_TU7_MP,
     {0x82012704, 0x63675F66, kTitleIdCODWaW, 0x9D64, NULL, 0x85914734,
      0x824AEBF0, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyGhosts_IW6_DEV_2iw6mp,
     {0x820BB320, 0x63675F66, kTitleIdCODGhostsDEV, 0x3A5C, NULL, 0x84BF6668,
      0x82FC6708, 0x82832AC8}},
    {CallOfDutyGame::GameBuild::CallOfDutyGhosts_IW6_DEV_1iw6sp,
     {0x82032648, 0x63675F66, kTitleIdCODGhostsDEV, 0x60, NULL, 0x84AF5CD4,
      0x82D81130, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyGhosts_IW6_DEV_4iw6sp_demo,
     {0x82013184, 0x63675F66, kTitleIdCODGhostsDEV, 0x60, NULL, 0x84254FC8,
      0x829EBF28, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyGhosts_IW6_DEV_default,
     {0x82021FC4, 0x63675F66, kTitleIdCODGhostsDEV, 0x60, NULL, 0x8418C234,
      0x82956468, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyAdvancedWarfare_TU17_SP,
     {0x82014B64, 0x63675F66, kTitleIdCODAW, 0x82BBD060, 0x82BBD05C, 0x82848844,
      NULL, NULL}},
    {CallOfDutyGame::GameBuild::CallOfDutyAdvancedWarfare_TU17_MP,
     {0x82066D7C, 0x63675F66, kTitleIdCODAW, 0x356C, NULL, 0x82928CA4,
      0x82B0D308, NULL}},
    {CallOfDutyGame::GameBuild::Wolfenstein_TU0_SP,
     {0x820BA060, 0x666F7600, kTitleIdWolfenstein, 0x160c, NULL, 0x8315CC70,
      0x830DE3A4, NULL}}};

CallOfDutyGame::~CallOfDutyGame() = default;

bool CallOfDutyGame::IsGameSupported() {
  auto title_id = kernel_state()->title_id();

  if (title_id != kTitleIdCOD4 && title_id != kTitleIdCOD3 &&
      title_id != kTitleIdCODBO2 && title_id != kTitleIdCODMW2 &&
      title_id != kTitleIdCODMW3 && title_id != kTitleIdCODNX1 &&
      title_id != kTitleIdCODWaW && title_id != kTitleIdCODGhostsDEV &&
      title_id != kTitleIdCODAW && title_id != kTitleIdWolfenstein) {
    return false;
  }

  for (auto& build : supported_builds) {
    if (build.second.title_id != title_id) {
      continue;
    }
    auto* build_ptr = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
        build.second.cg_fov_address);
    // printf("Processing build: %d\n", static_cast<int>(build.first));
    // printf("ADDRESS IS THIS: 0x%08X\n", build.second.cg_fov_address);
    if (*build_ptr == build.second.cg_fov) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

bool CallOfDutyGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                             X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }
  if (supported_builds[game_build_].Dvar_GetBool_address != NULL) {
    if (!Dvar_GetBool("cl_ingame",
                      supported_builds[game_build_].Dvar_GetBool_address)) {
      return false;
    }
  }

  xe::be<float>* degree_x;
  xe::be<float>* degree_y;

  if (supported_builds[game_build_].base_address != NULL) {
    // Calculate based on base address
    uint32_t base_address =
        *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].base_address);
    if (!base_address || base_address == NULL) {
      // Not in game
      return false;
    }

    int32_t offset = supported_builds[game_build_].x_address;
    /* uint32_t stored_base_address;
    if (base_address && base_address >= 0x40000000) {
      stored_base_address = base_address;

    }
    */
    degree_x = kernel_memory()->TranslateVirtual<xe::be<float>*>(base_address +
                                                                 offset);
    degree_y = kernel_memory()->TranslateVirtual<xe::be<float>*>(base_address +
                                                                 offset - 4);

  } else {
    // Use pre-defined addresses for other builds
    degree_x = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        supported_builds[game_build_].x_address);
    degree_y = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        supported_builds[game_build_].y_address);
  }
  xe::be<float>* fovscale = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].fovscale_address);

  float new_degree_x = *degree_x;
  float new_degree_y = *degree_y;
  float calc_fovscale = *fovscale;

  if (calc_fovscale <= 0.f ||
      calc_fovscale >
          1.0f /*for when cg_fovscale is used*/) {  // Required check otherwise
                                                    // mouse stops working.
    calc_fovscale = 1.f;
  }
  const float a =
      (float)cvars::fov_sensitivity;  // Quadratic scaling to make
                                      // fovscale effect sens stronger
  if (calc_fovscale != 1.f) {
    calc_fovscale =
        (1 - a) * (calc_fovscale * calc_fovscale) + a * calc_fovscale;
  }

  float divsor;
  divsor = 10.5f / calc_fovscale;

  float camX = (float)*degree_x;
  float camY = (float)*degree_y;

  // X-axis = 0 to 360
  if (!cvars::invert_x) {
    camX -= (input_state.mouse.x_delta / divsor) * (float)cvars::sensitivity;
  } else {
    camX += (input_state.mouse.x_delta / divsor) * (float)cvars::sensitivity;
  }
  *degree_x = camX;

  if (!cvars::invert_y) {
    camY += (input_state.mouse.y_delta / divsor) * (float)cvars::sensitivity;
  } else {
    camY -= (input_state.mouse.y_delta / divsor) * (float)cvars::sensitivity;
  }
  *degree_y = camY;

  return true;
}

std::string CallOfDutyGame::ChooseBinds() { return "Default"; }

bool CallOfDutyGame::ModifierKeyHandler(uint32_t user_index,
                                        RawInputState& input_state,
                                        X_INPUT_STATE* out_state) {
  if (kernel_state()->title_id() == kTitleIdCODAW) {
    // AW has broken bloom in some maps, toggling readback_resolve fixes it.
    static std::chrono::steady_clock::time_point last_toggle_time;
    const std::chrono::milliseconds toggle_delay(500);

    auto now = std::chrono::steady_clock::now();

    if (now - last_toggle_time > toggle_delay) {
      cvars::d3d12_readback_resolve = !cvars::d3d12_readback_resolve;

      last_toggle_time = now;
    }
    return true;
  } else
    return false;
}
bool CallOfDutyGame::Dvar_GetBool(std::string dvar, uint32_t dvar_address) {
  XThread* current_thread = XThread::GetCurrentThread();

  if (dvar_address == NULL) {
    return false;
  }

  std::string in_game_str = dvar;

  uint32_t command_ptr = kernel_state()->memory()->SystemHeapAlloc(100);

  char* command_addr =
      kernel_state()->memory()->TranslateVirtual<char*>(command_ptr);
  strcpy(command_addr, in_game_str.c_str());

  current_thread->thread_state()->context()->r[3] = command_ptr;

  kernel_state()->processor()->Execute(current_thread->thread_state(),
                                       dvar_address);

  bool state = current_thread->thread_state()->context()->r[3];

  return state;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe