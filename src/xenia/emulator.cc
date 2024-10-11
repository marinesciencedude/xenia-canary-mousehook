/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/emulator.h"

#include <algorithm>
#include <cinttypes>

#include "config.h"
#include "third_party/fmt/include/fmt/format.h"
#include "third_party/tabulate/single_include/tabulate/tabulate.hpp"
#include "third_party/zarchive/include/zarchive/zarchivecommon.h"
#include "third_party/zarchive/include/zarchive/zarchivewriter.h"
#include "third_party/zarchive/src/sha_256.h"
#include "xenia/apu/audio_system.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/base/system.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/backend/null_backend.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/hid/input_driver.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/gameinfo_utils.h"
#include "xenia/kernel/util/xdbf_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xbdm/xbdm_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_module.h"
#include "xenia/memory.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_host_notification.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/disc_zarchive_device.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/null_device.h"
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/vfs/virtual_file_system.h"

#if XE_ARCH_AMD64
#include "xenia/cpu/backend/x64/x64_backend.h"
#endif  // XE_ARCH

DEFINE_double(time_scalar, 1.0,
              "Scalar used to speed or slow time (1x, 2x, 1/2x, etc).",
              "General");

DEFINE_string(
    launch_module, "",
    "Executable to launch from the .iso or the package instead of default.xex "
    "or the module specified by the game. Leave blank to launch the default "
    "module.",
    "General");

DEFINE_bool(ge_remove_blur, false,
            "(GoldenEye) Removes low-res blur when in classic-graphics mode",
            "MouseHook");
DEFINE_bool(ge_debug_menu, false,
            "(GoldenEye) Enables the debug menu, accessible with LB/1",
            "MouseHook");
DEFINE_bool(sr2_better_drive_cam, true,
            "(Saints Row 2) unties X rotation from vehicles when "
            "auto-centering is disabled akin to GTA IV.",
            "MouseHook");

DEFINE_bool(sr2_better_handbrake_cam, true,
            "(Saints Row 2) unties X rotation from vehicles when "
            "handbraking akin to SR1.",
            "MouseHook");

DEFINE_bool(allow_game_relative_writes, false,
            "Not useful to non-developers. Allows code to write to paths "
            "relative to game://. Used for "
            "generating test data to compare with original hardware. ",
            "General");

DECLARE_int32(user_language);

DECLARE_bool(allow_plugins);
DECLARE_bool(disable_autoaim);

namespace xe {
using namespace xe::literals;

Emulator::GameConfigLoadCallback::GameConfigLoadCallback(Emulator& emulator)
    : emulator_(emulator) {
  emulator_.AddGameConfigLoadCallback(this);
}

Emulator::GameConfigLoadCallback::~GameConfigLoadCallback() {
  emulator_.RemoveGameConfigLoadCallback(this);
}

Emulator::Emulator(const std::filesystem::path& command_line,
                   const std::filesystem::path& storage_root,
                   const std::filesystem::path& content_root,
                   const std::filesystem::path& cache_root)
    : on_launch(),
      on_terminate(),
      on_exit(),
      command_line_(command_line),
      storage_root_(storage_root),
      content_root_(content_root),
      cache_root_(cache_root),
      title_name_(),
      title_version_(),
      display_window_(nullptr),
      memory_(),
      audio_system_(),
      graphics_system_(),
      input_system_(),
      export_resolver_(),
      file_system_(),
      kernel_state_(),
      main_thread_(),
      title_id_(std::nullopt),
      game_info_database_(),
      paused_(false),
      restoring_(false),
      restore_fence_() {
#if XE_PLATFORM_WIN32 == 1
  // Show a disclaimer that links to the quickstart
  // guide the first time they ever open the emulator
  uint64_t persistent_flags = GetPersistentEmulatorFlags();
  if (!(persistent_flags & EmulatorFlagDisclaimerAcknowledged)) {
    if ((MessageBoxW(
             nullptr,
             L"DISCLAIMER: Xenia is not for enabling illegal activity, and "
             "support is unavailable for illegally obtained software.\n\n"
             "Please respect this policy as no further reminders will be "
             "given.\n\nThe quickstart guide explains how to use digital or "
             "physical games from your Xbox 360 console.\n\nWould you like "
             "to open it?",
             L"Xenia", MB_YESNO | MB_ICONQUESTION) == IDYES)) {
      LaunchWebBrowser(
          "https://github.com/xenia-canary/xenia-canary/wiki/"
          "Quickstart#how-to-rip-games");
    }
    SetPersistentEmulatorFlags(persistent_flags |
                               EmulatorFlagDisclaimerAcknowledged);
  }
#endif
}

Emulator::~Emulator() {
  // Note that we delete things in the reverse order they were initialized.

  // Give the systems time to shutdown before we delete them.
  if (graphics_system_) {
    graphics_system_->Shutdown();
  }
  if (audio_system_) {
    audio_system_->Shutdown();
  }

  input_system_.reset();
  graphics_system_.reset();
  audio_system_.reset();

  kernel_state_.reset();
  file_system_.reset();

  processor_.reset();

  export_resolver_.reset();

  ExceptionHandler::Uninstall(Emulator::ExceptionCallbackThunk, this);
}

X_STATUS Emulator::Setup(
    ui::Window* display_window, ui::ImGuiDrawer* imgui_drawer,
    bool require_cpu_backend,
    std::function<std::unique_ptr<apu::AudioSystem>(cpu::Processor*)>
        audio_system_factory,
    std::function<std::unique_ptr<gpu::GraphicsSystem>()>
        graphics_system_factory,
    std::function<std::vector<std::unique_ptr<hid::InputDriver>>(ui::Window*)>
        input_driver_factory) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  display_window_ = display_window;
  imgui_drawer_ = imgui_drawer;

  // Initialize clock.
  // 360 uses a 50MHz clock.
  Clock::set_guest_tick_frequency(50000000);
  // We could reset this with save state data/constant value to help replays.
  Clock::set_guest_system_time_base(Clock::QueryHostSystemTime());
  // This can be adjusted dynamically, as well.
  Clock::set_guest_time_scalar(cvars::time_scalar);

  // Before we can set thread affinity we must enable the process to use all
  // logical processors.
  xe::threading::EnableAffinityConfiguration();

  // Create memory system first, as it is required for other systems.
  memory_ = std::make_unique<Memory>();
  if (!memory_->Initialize()) {
    return false;
  }

  // Shared export resolver used to attach and query for HLE exports.
  export_resolver_ = std::make_unique<xe::cpu::ExportResolver>();

  std::unique_ptr<xe::cpu::backend::Backend> backend;
#if XE_ARCH_AMD64
  if (cvars::cpu == "x64") {
    backend.reset(new xe::cpu::backend::x64::X64Backend());
  }
#endif  // XE_ARCH
  if (cvars::cpu == "any") {
    if (!backend) {
#if XE_ARCH_AMD64
      backend.reset(new xe::cpu::backend::x64::X64Backend());
#endif  // XE_ARCH
    }
  }
  if (!backend && !require_cpu_backend) {
    backend.reset(new xe::cpu::backend::NullBackend());
  }

  // Initialize the CPU.
  processor_ = std::make_unique<xe::cpu::Processor>(memory_.get(),
                                                    export_resolver_.get());
  if (!processor_->Setup(std::move(backend))) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Initialize the APU.
  if (audio_system_factory) {
    audio_system_ = audio_system_factory(processor_.get());
    if (!audio_system_) {
      return X_STATUS_NOT_IMPLEMENTED;
    }
  }

  // Initialize the GPU.
  graphics_system_ = graphics_system_factory();
  if (!graphics_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Initialize the HID.
  input_system_ = std::make_unique<xe::hid::InputSystem>(display_window_);
  if (!input_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }
  if (input_driver_factory) {
    auto input_drivers = input_driver_factory(display_window_);
    for (size_t i = 0; i < input_drivers.size(); ++i) {
      auto& input_driver = input_drivers[i];
      input_driver->set_is_active_callback(
          []() -> bool { return !xe::kernel::xam::xeXamIsUIActive(); });
      input_system_->AddDriver(std::move(input_driver));
    }
  }

  result = input_system_->Setup();
  if (result) {
    return result;
  }

  // Bring up the virtual filesystem used by the kernel.
  file_system_ = std::make_unique<xe::vfs::VirtualFileSystem>();

  patcher_ = std::make_unique<xe::patcher::Patcher>(storage_root_);

  // Shared kernel state.
  kernel_state_ = std::make_unique<xe::kernel::KernelState>(this);
#define LOAD_KERNEL_MODULE(t) \
  static_cast<void>(kernel_state_->LoadKernelModule<kernel::t>())
  // HLE kernel modules.
  LOAD_KERNEL_MODULE(xboxkrnl::XboxkrnlModule);
  LOAD_KERNEL_MODULE(xam::XamModule);
  LOAD_KERNEL_MODULE(xbdm::XbdmModule);
#undef LOAD_KERNEL_MODULE
  plugin_loader_ = std::make_unique<xe::patcher::PluginLoader>(
      kernel_state_.get(), storage_root() / "plugins");

  // Setup the core components.
  result = graphics_system_->Setup(
      processor_.get(), kernel_state_.get(),
      display_window_ ? &display_window_->app_context() : nullptr,
      display_window_ != nullptr);
  if (result) {
    return result;
  }

  if (audio_system_) {
    result = audio_system_->Setup(kernel_state_.get());
    if (result) {
      return result;
    }
  }

  // Initialize emulator fallback exception handling last.
  ExceptionHandler::Install(Emulator::ExceptionCallbackThunk, this);

  return result;
}

X_STATUS Emulator::TerminateTitle() {
  if (!is_title_open()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  kernel_state_->TerminateTitle();
  title_id_ = std::nullopt;
  title_name_ = "";
  title_version_ = "";
  on_terminate();
  return X_STATUS_SUCCESS;
}

const std::unique_ptr<vfs::Device> Emulator::CreateVfsDevice(
    const std::filesystem::path& path, const std::string_view mount_path) {
  // Must check if the type has changed e.g. XamSwapDisc
  switch (GetFileSignature(path)) {
    case FileSignatureType::XEX1:
    case FileSignatureType::XEX2:
    case FileSignatureType::ELF: {
      auto parent_path = path.parent_path();
      return std::make_unique<vfs::HostPathDevice>(
          mount_path, parent_path, !cvars::allow_game_relative_writes);
    } break;
    case FileSignatureType::LIVE:
    case FileSignatureType::CON:
    case FileSignatureType::PIRS: {
      return vfs::XContentContainerDevice::CreateContentDevice(mount_path,
                                                               path);
    } break;
    case FileSignatureType::XISO: {
      return std::make_unique<vfs::DiscImageDevice>(mount_path, path);
    } break;
    case FileSignatureType::ZAR: {
      return std::make_unique<vfs::DiscZarchiveDevice>(mount_path, path);
    } break;
    case FileSignatureType::EXE:
    case FileSignatureType::Unknown:
    default:
      return nullptr;
      break;
  }
}

uint64_t Emulator::GetPersistentEmulatorFlags() {
#if XE_PLATFORM_WIN32 == 1
  uint64_t value = 0;
  DWORD value_size = sizeof(value);
  HKEY xenia_hkey = nullptr;
  LSTATUS lstat =
      RegOpenKeyA(HKEY_CURRENT_USER, "SOFTWARE\\Xenia", &xenia_hkey);
  if (!xenia_hkey) {
    // let the Set function create the key and initialize it to 0
    SetPersistentEmulatorFlags(0ULL);
    return 0ULL;
  }

  lstat = RegQueryValueExA(xenia_hkey, "XEFLAGS", 0, NULL,
                           reinterpret_cast<LPBYTE>(&value), &value_size);
  RegCloseKey(xenia_hkey);
  if (lstat) {
    return 0ULL;
  }
  return value;
#else
  return EmulatorFlagDisclaimerAcknowledged;
#endif
}
void Emulator::SetPersistentEmulatorFlags(uint64_t new_flags) {
#if XE_PLATFORM_WIN32 == 1
  uint64_t value = new_flags;
  DWORD value_size = sizeof(value);
  HKEY xenia_hkey = nullptr;
  LSTATUS lstat =
      RegOpenKeyA(HKEY_CURRENT_USER, "SOFTWARE\\Xenia", &xenia_hkey);
  if (!xenia_hkey) {
    lstat = RegCreateKeyA(HKEY_CURRENT_USER, "SOFTWARE\\Xenia", &xenia_hkey);
  }

  lstat = RegSetValueExA(xenia_hkey, "XEFLAGS", 0, REG_QWORD,
                         reinterpret_cast<const BYTE*>(&value), 8);
  RegFlushKey(xenia_hkey);
  RegCloseKey(xenia_hkey);
#endif
}

X_STATUS Emulator::MountPath(const std::filesystem::path& path,
                             const std::string_view mount_path) {
  auto device = CreateVfsDevice(path, mount_path);
  if (!device || !device->Initialize()) {
    XELOGE(
        "Unable to mount the selected file, it is an unsupported format or "
        "corrupted.");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    XELOGE("Unable to register the input file to {}.",
           xe::path_to_utf8(mount_path));
    return X_STATUS_NO_SUCH_FILE;
  }

  file_system_->UnregisterSymbolicLink(kDefaultPartitionSymbolicLink);
  file_system_->UnregisterSymbolicLink(kDefaultGameSymbolicLink);
  file_system_->UnregisterSymbolicLink("plugins:");

  // Create symlinks to the device.
  file_system_->RegisterSymbolicLink(kDefaultGameSymbolicLink, mount_path);
  file_system_->RegisterSymbolicLink(kDefaultPartitionSymbolicLink, mount_path);

  return X_STATUS_SUCCESS;
}

Emulator::FileSignatureType Emulator::GetFileSignature(
    const std::filesystem::path& path) {
  FILE* file = xe::filesystem::OpenFile(path, "rb");

  if (!file) {
    return FileSignatureType::Unknown;
  }

  const uint64_t file_size = std::filesystem::file_size(path);
  const int64_t header_size = 4;

  if (file_size < header_size) {
    return FileSignatureType::Unknown;
  }

  char file_magic[header_size];
  fread_s(file_magic, sizeof(file_magic), 1, header_size, file);

  fourcc_t magic_value =
      make_fourcc(file_magic[0], file_magic[1], file_magic[2], file_magic[3]);

  fclose(file);

  switch (magic_value) {
    case xe::cpu::kXEX1Signature:
      return FileSignatureType::XEX1;
    case xe::cpu::kXEX2Signature:
      return FileSignatureType::XEX2;
    case xe::vfs::kCONSignature:
      return FileSignatureType::CON;
    case xe::vfs::kLIVESignature:
      return FileSignatureType::LIVE;
    case xe::vfs::kPIRSSignature:
      return FileSignatureType::PIRS;
    case xe::vfs::kXSFSignature:
      return FileSignatureType::XISO;
    case xe::cpu::kElfSignature:
      return FileSignatureType::ELF;
    default:
      break;
  }

  magic_value = make_fourcc(file_magic[0], file_magic[1], 0, 0);

  if (xe::kernel::kEXESignature == magic_value) {
    return FileSignatureType::EXE;
  }

  file = xe::filesystem::OpenFile(path, "rb");
  xe::filesystem::Seek(file, -header_size, SEEK_END);
  fread_s(file_magic, sizeof(file_magic), 1, header_size, file);
  fclose(file);

  magic_value =
      make_fourcc(file_magic[0], file_magic[1], file_magic[2], file_magic[3]);

  if (xe::vfs::kZarMagic == magic_value) {
    return FileSignatureType::ZAR;
  }

  // Check if XISO
  std::unique_ptr<vfs::Device> device =
      std::make_unique<vfs::DiscImageDevice>("", path);

  XELOGI("Checking for XISO");

  if (device->Initialize()) {
    return FileSignatureType::XISO;
  }

  return FileSignatureType::Unknown;
}

X_STATUS Emulator::LaunchPath(const std::filesystem::path& path) {
  X_STATUS mount_result = X_STATUS_SUCCESS;

  switch (GetFileSignature(path)) {
    case FileSignatureType::XEX1:
    case FileSignatureType::XEX2:
    case FileSignatureType::ELF: {
      mount_result = MountPath(path, "\\Device\\Harddisk0\\Partition1");
      return mount_result ? mount_result : LaunchXexFile(path);
    } break;
    case FileSignatureType::LIVE:
    case FileSignatureType::CON:
    case FileSignatureType::PIRS: {
      mount_result = MountPath(path, "\\Device\\Cdrom0");
      return mount_result ? mount_result : LaunchStfsContainer(path);
    } break;
    case FileSignatureType::XISO: {
      mount_result = MountPath(path, "\\Device\\Cdrom0");
      return mount_result ? mount_result : LaunchDiscImage(path);
    } break;
    case FileSignatureType::ZAR: {
      mount_result = MountPath(path, "\\Device\\Cdrom0");
      return mount_result ? mount_result : LaunchDiscArchive(path);
    } break;
    case FileSignatureType::EXE:
    case FileSignatureType::Unknown:
    default:
      return X_STATUS_NOT_SUPPORTED;
      break;
  }
}

X_STATUS Emulator::LaunchXexFile(const std::filesystem::path& path) {
  // We create a virtual filesystem pointing to its directory and symlink
  // that to the game filesystem.
  // e.g., /my/files/foo.xex will get a local fs at:
  // \\Device\\Harddisk0\\Partition1
  // and then get that symlinked to game:\, so
  // -> game:\foo.xex
  // Get just the filename (foo.xex).
  auto file_name = path.filename();

  // Launch the game.
  auto fs_path = "game:\\" + xe::path_to_utf8(file_name);
  X_STATUS result = CompleteLaunch(path, fs_path);

  if (XSUCCEEDED(result)) {
    kernel_state_->deployment_type_ = XDeploymentType::kHardDrive;
    if (!kernel_state_->is_title_system_type(title_id())) {
      // Assumption that any loaded game is loaded as a disc.
      kernel_state_->deployment_type_ = XDeploymentType::kOpticalDisc;
    }
  }
  return result;
}

X_STATUS Emulator::LaunchDiscImage(const std::filesystem::path& path) {
  std::string module_path = FindLaunchModule();
  X_STATUS result = CompleteLaunch(path, module_path);

  if (result == X_STATUS_NOT_FOUND && !cvars::launch_module.empty()) {
    return LaunchDefaultModule(path);
  }
  kernel_state_->deployment_type_ = XDeploymentType::kOpticalDisc;
  return result;
}

X_STATUS Emulator::LaunchDiscArchive(const std::filesystem::path& path) {
  std::string module_path = FindLaunchModule();
  X_STATUS result = CompleteLaunch(path, module_path);

  if (result == X_STATUS_NOT_FOUND && !cvars::launch_module.empty()) {
    return LaunchDefaultModule(path);
  }
  kernel_state_->deployment_type_ = XDeploymentType::kOpticalDisc;
  return result;
}

X_STATUS Emulator::LaunchStfsContainer(const std::filesystem::path& path) {
  std::string module_path = FindLaunchModule();
  X_STATUS result = CompleteLaunch(path, module_path);

  if (result == X_STATUS_NOT_FOUND && !cvars::launch_module.empty()) {
    return LaunchDefaultModule(path);
  }
  kernel_state_->deployment_type_ = XDeploymentType::kGoD;
  return result;
}

X_STATUS Emulator::LaunchDefaultModule(const std::filesystem::path& path) {
  cvars::launch_module = "";
  std::string module_path = FindLaunchModule();
  X_STATUS result = CompleteLaunch(path, module_path);

  if (XSUCCEEDED(result)) {
    kernel_state_->deployment_type_ = XDeploymentType::kHardDrive;
    if (!kernel_state_->is_title_system_type(title_id())) {
      // Assumption that any loaded game is loaded as a disc.
      kernel_state_->deployment_type_ = XDeploymentType::kOpticalDisc;
    }
  }
  return result;
}

X_STATUS Emulator::DataMigration(const uint64_t xuid) {
  uint32_t failure_count = 0;
  const std::string xuid_string = fmt::format("{:016X}", xuid);
  const std::string common_xuid_string = fmt::format("{:016X}", 0);
  const std::filesystem::path path_to_profile_data =
      content_root_ / xuid_string / "FFFE07D1" / "00010000" / xuid_string;
  // Filter directories inside. First we need to find any content type
  // directories.
  // Savefiles must go to user specific directory
  // Everything else goes to common
  const auto titles_to_move = xe::filesystem::FilterByName(
      xe::filesystem::ListDirectories(content_root_),
      std::regex("[A-F0-9]{8}"));

  for (const auto& title : titles_to_move) {
    if (xe::path_to_utf8(title.name) == "FFFE07D1" ||
        xe::path_to_utf8(title.name) == "00000000") {
      // SKip any dashboard/profile related data that was previously installed
      continue;
    }

    const auto content_type_dirs = xe::filesystem::FilterByName(
        xe::filesystem::ListDirectories(title.path / title.name),
        std::regex("[A-F0-9]{8}"));

    for (const auto& content_type : content_type_dirs) {
      const std::string used_xuid =
          xe::path_to_utf8(content_type.name) == "00000001"
              ? xuid_string
              : common_xuid_string;

      const auto previous_path = content_root_ / title.name / content_type.name;
      const auto path = content_root_ / used_xuid / title.name;

      if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
      }

      std::error_code ec;
      std::filesystem::rename(previous_path, path / content_type.name, ec);

      if (ec) {
        failure_count++;
        XELOGW("{}: Moving from: {} to: {} failed! Error message: {} ({:08X})",
               __func__, xe::path_to_utf8(previous_path),
               xe::path_to_utf8(path / content_type.name), ec.message(),
               ec.value());
      }
    }
    // Other directories:
    // Headers - Just copy everything to both common and xuid locations
    // profile - ?
    if (std::filesystem::exists(title.path / title.name / "Headers")) {
      const auto xuid_path =
          content_root_ / xuid_string / title.name / "Headers";

      std::filesystem::create_directories(xuid_path);

      std::error_code ec;
      // Copy to specific user
      std::filesystem::copy(title.path / title.name / "Headers", xuid_path,
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::skip_existing,
                            ec);
      if (ec) {
        failure_count++;
        XELOGW("{}: Copying from: {} to: {} failed! Error message: {} ({:08X})",
               __func__, xe::path_to_utf8(title.path / title.name / "Headers"),
               xe::path_to_utf8(xuid_path), ec.message(), ec.value());
      }

      const auto header_types =
          xe::filesystem::ListDirectories(title.path / title.name / "Headers");

      if (!(header_types.size() == 1 &&
            header_types.at(0).name == "00000001")) {
        const auto common_path =
            content_root_ / common_xuid_string / title.name / "Headers";

        std::filesystem::create_directories(common_path);

        // Copy to common, skip cases where only savefile header is available
        std::filesystem::copy(title.path / title.name / "Headers", common_path,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::skip_existing,
                              ec);
        if (ec) {
          failure_count++;
          XELOGW(
              "{}: Copying from: {} to: {} failed! Error message: {} ({:08X})",
              __func__, xe::path_to_utf8(title.path / title.name / "Headers"),
              xe::path_to_utf8(common_path), ec.message(), ec.value());
        }
      }

      if (!ec) {
        // Remove previous directory
        std::error_code ec;
        std::filesystem::remove_all(title.path / title.name / "Headers", ec);
      }
    }

    if (std::filesystem::exists(title.path / title.name / "profile")) {
      // Find directory with previous username. There should be only one!
      const auto old_profile_data =
          xe::filesystem::ListDirectories(title.path / title.name / "profile");

      xe::filesystem::FileInfo& entry_to_copy = xe::filesystem::FileInfo();
      if (old_profile_data.size() != 1) {
        for (const auto& entry : old_profile_data) {
          if (entry.name == "User") {
            entry_to_copy = entry;
          }
        }
      } else {
        entry_to_copy = old_profile_data.front();
      }

      const auto path_from =
          title.path / title.name / "profile" / entry_to_copy.name;
      std::error_code ec;
      // Move files from inside to outside for convenience
      std::filesystem::rename(path_from, path_to_profile_data / title.name, ec);
      if (ec) {
        failure_count++;
        XELOGW("{}: Moving from: {} to: {} failed! Error message: {} ({:08X})",
               __func__, xe::path_to_utf8(path_from),
               xe::path_to_utf8(path_to_profile_data / title.name),
               ec.message(), ec.value());
      } else {
        std::error_code ec;
        std::filesystem::remove_all(title.path / title.name / "profile", ec);
      }
    }

    const auto remaining_file_list =
        xe::filesystem::ListDirectories(title.path / title.name);

    if (remaining_file_list.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(title.path / title.name, ec);
    }
  }

  std::string migration_status_message =
      fmt::format("Migration finished with {} {}.", failure_count,
                  failure_count == 1 ? "error" : "errors");

  if (failure_count) {
    migration_status_message.append(
        " For more information check xenia.log file.");
  }
  new xe::ui::HostNotificationWindow(imgui_drawer_, "Migration Status",
                                     migration_status_message, 0);
  return X_STATUS_SUCCESS;
}

X_STATUS Emulator::InstallContentPackage(
    const std::filesystem::path& path,
    ContentInstallationInfo& installation_info) {
  std::unique_ptr<vfs::Device> device =
      vfs::XContentContainerDevice::CreateContentDevice("", path);

  installation_info.content_name = "Invalid Content Package!";
  installation_info.content_type = static_cast<XContentType>(0);
  installation_info.installation_path = xe::path_to_utf8(path.filename());

  if (!device || !device->Initialize()) {
    XELOGE("Failed to initialize device");
    return X_STATUS_INVALID_PARAMETER;
  }

  const vfs::XContentContainerDevice* dev =
      (vfs::XContentContainerDevice*)device.get();

  std::filesystem::path installation_path =
      content_root() / fmt::format("{:016X}", dev->xuid()) /
      fmt::format("{:08X}", dev->title_id()) /
      fmt::format("{:08X}", dev->content_type()) / path.filename();

  std::filesystem::path header_path =
      content_root() / fmt::format("{:016X}", dev->xuid()) /
      fmt::format("{:08X}", dev->title_id()) / "Headers" /
      fmt::format("{:08X}", dev->content_type()) / path.filename();

  installation_info.installation_path =
      fmt::format("{:016X}/{:08X}/{:08X}/{}", dev->xuid(), dev->title_id(),
                  dev->content_type(), xe::path_to_utf8(path.filename()));

  installation_info.content_name =
      xe::to_utf8(dev->content_header().display_name());
  installation_info.content_type =
      static_cast<XContentType>(dev->content_type());

  if (std::filesystem::exists(installation_path)) {
    // TODO(Gliniak): Popup
    // Do you want to overwrite already existing data?
  } else {
    std::error_code error_code;
    std::filesystem::create_directories(installation_path, error_code);
    if (error_code) {
      installation_info.content_name = "Cannot Create Content Directory!";
      return error_code.value();
    }
  }

  vfs::VirtualFileSystem::ExtractContentHeader(device.get(), header_path);

  X_STATUS error_code = vfs::VirtualFileSystem::ExtractContentFiles(
      device.get(), installation_path);
  if (error_code != X_ERROR_SUCCESS) {
    return error_code;
  }

  kernel_state()->BroadcastNotification(kXNotificationIDLiveContentInstalled,
                                        0);

  return error_code;
}

X_STATUS Emulator::ExtractZarchivePackage(
    const std::filesystem::path& path,
    const std::filesystem::path& extract_dir) {
  std::unique_ptr<vfs::Device> device =
      std::make_unique<vfs::DiscZarchiveDevice>("", path);
  if (!device->Initialize()) {
    XELOGE("Failed to initialize device");
    return X_STATUS_INVALID_PARAMETER;
  }

  if (std::filesystem::exists(extract_dir)) {
    // TODO(Gliniak): Popup
    // Do you want to overwrite already existing data?
  } else {
    std::error_code error_code;
    std::filesystem::create_directories(extract_dir, error_code);
    if (error_code) {
      return error_code.value();
    }
  }

  return vfs::VirtualFileSystem::ExtractContentFiles(device.get(), extract_dir);
}

X_STATUS Emulator::CreateZarchivePackage(
    const std::filesystem::path& inputDirectory,
    const std::filesystem::path& outputFile) {
  std::vector<uint8_t> buffer;
  buffer.resize(64 * 1024);

  std::error_code ec;
  PackContext packContext;
  packContext.outputFilePath = outputFile;

  ZArchiveWriter zWriter(
      [](int32_t partIndex, void* ctx) {
        PackContext* packContext = reinterpret_cast<PackContext*>(ctx);
        packContext->currentOutputFile =
            std::ofstream(packContext->outputFilePath, std::ios::binary);

        if (!packContext->currentOutputFile.is_open()) {
          XELOGI("Failed to create output file: {}\n",
                 packContext->outputFilePath.string());
          packContext->hasError = true;
        }
      },
      [](const void* data, size_t length, void* ctx) {
        PackContext* packContext = reinterpret_cast<PackContext*>(ctx);
        packContext->currentOutputFile.write(
            reinterpret_cast<const char*>(data), length);
      },
      &packContext);

  if (packContext.hasError) {
    return X_STATUS_UNSUCCESSFUL;
  }

  for (auto const& dirEntry :
       std::filesystem::recursive_directory_iterator(inputDirectory)) {
    std::filesystem::path pathEntry =
        std::filesystem::relative(dirEntry.path(), inputDirectory, ec);

    if (ec) {
      XELOGI("Failed to get relative path {}\n", pathEntry.string());
      return X_STATUS_UNSUCCESSFUL;
    }

    if (dirEntry.is_directory()) {
      if (!zWriter.MakeDir(pathEntry.generic_string().c_str(), false)) {
        XELOGI("Failed to create directory {}\n", pathEntry.string());
        return X_STATUS_UNSUCCESSFUL;
      }
    } else if (dirEntry.is_regular_file()) {
      // Don't pack itself to prevent infinite packing.
      if (dirEntry == outputFile) {
        continue;
      }

      XELOGI("Adding file: {}\n", pathEntry.string());

      if (!zWriter.StartNewFile(pathEntry.generic_string().c_str())) {
        XELOGI("Failed to create archive file {}\n", pathEntry.string());
        return X_STATUS_UNSUCCESSFUL;
      }

      std::filesystem::path file_to_pack_path = inputDirectory / pathEntry;
      FILE* file = xe::filesystem::OpenFile(file_to_pack_path, "rb");

      if (!file) {
        XELOGI("Failed to open input file {}\n", pathEntry.string());
        return X_STATUS_UNSUCCESSFUL;
      }

      const uint64_t file_size = std::filesystem::file_size(file_to_pack_path);
      uint64_t total_bytes_read = 0;

      while (total_bytes_read < file_size) {
        uint64_t bytes_read =
            fread_s(buffer.data(), buffer.size(), 1, buffer.size(), file);

        total_bytes_read += bytes_read;

        zWriter.AppendData(buffer.data(), bytes_read);
      }

      fclose(file);
    }

    if (packContext.hasError) {
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  zWriter.Finalize();

  return X_STATUS_SUCCESS;
}

void Emulator::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  // Don't hold the lock on this (so any waits follow through)
  graphics_system_->Pause();
  audio_system_->Pause();

  auto lock = global_critical_region::AcquireDirect();
  auto threads =
      kernel_state()->object_table()->GetObjectsByType<kernel::XThread>(
          kernel::XObject::Type::Thread);
  auto current_thread = kernel::XThread::IsInThread()
                            ? kernel::XThread::GetCurrentThread()
                            : nullptr;
  for (auto thread : threads) {
    // Don't pause ourself or host threads.
    if (thread == current_thread || !thread->can_debugger_suspend()) {
      continue;
    }

    if (thread->is_running()) {
      thread->thread()->Suspend(nullptr);
    }
  }

  XELOGD("! EMULATOR PAUSED !");
}

void Emulator::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  XELOGD("! EMULATOR RESUMED !");

  graphics_system_->Resume();
  audio_system_->Resume();

  auto threads =
      kernel_state()->object_table()->GetObjectsByType<kernel::XThread>(
          kernel::XObject::Type::Thread);
  for (auto thread : threads) {
    if (!thread->can_debugger_suspend()) {
      // Don't pause host threads.
      continue;
    }

    if (thread->is_running()) {
      thread->thread()->Resume(nullptr);
    }
  }
}

bool Emulator::SaveToFile(const std::filesystem::path& path) {
  Pause();

  filesystem::CreateEmptyFile(path);
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kReadWrite, 0, 2_GiB);
  if (!map) {
    return false;
  }

  // Save the emulator state to a file
  ByteStream stream(map->data(), map->size());
  stream.Write(kEmulatorSaveSignature);
  stream.Write(title_id_.has_value());
  if (title_id_.has_value()) {
    stream.Write(title_id_.value());
  }

  // It's important we don't hold the global lock here! XThreads need to step
  // forward (possibly through guarded regions) without worry!
  processor_->Save(&stream);
  graphics_system_->Save(&stream);
  audio_system_->Save(&stream);
  kernel_state_->Save(&stream);
  memory_->Save(&stream);
  map->Close(stream.offset());

  Resume();
  return true;
}

bool Emulator::RestoreFromFile(const std::filesystem::path& path) {
  // Restore the emulator state from a file
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kReadWrite);
  if (!map) {
    return false;
  }

  restoring_ = true;

  // Terminate any loaded titles.
  Pause();
  kernel_state_->TerminateTitle();

  auto lock = global_critical_region::AcquireDirect();
  ByteStream stream(map->data(), map->size());
  if (stream.Read<uint32_t>() != kEmulatorSaveSignature) {
    return false;
  }

  auto has_title_id = stream.Read<bool>();
  std::optional<uint32_t> title_id;
  if (!has_title_id) {
    title_id = {};
  } else {
    title_id = stream.Read<uint32_t>();
  }
  if (title_id_.has_value() != title_id.has_value() ||
      title_id_.value() != title_id.value()) {
    // Swapping between titles is unsupported at the moment.
    assert_always();
    return false;
  }

  if (!processor_->Restore(&stream)) {
    XELOGE("Could not restore processor!");
    return false;
  }
  if (!graphics_system_->Restore(&stream)) {
    XELOGE("Could not restore graphics system!");
    return false;
  }
  if (!audio_system_->Restore(&stream)) {
    XELOGE("Could not restore audio system!");
    return false;
  }
  if (!kernel_state_->Restore(&stream)) {
    XELOGE("Could not restore kernel state!");
    return false;
  }
  if (!memory_->Restore(&stream)) {
    XELOGE("Could not restore memory!");
    return false;
  }

  // Update the main thread.
  auto threads =
      kernel_state_->object_table()->GetObjectsByType<kernel::XThread>();
  for (auto thread : threads) {
    if (thread->main_thread()) {
      main_thread_ = thread;
      break;
    }
  }

  Resume();

  restore_fence_.Signal();
  restoring_ = false;

  return true;
}

const std::filesystem::path Emulator::GetNewDiscPath(
    std::string window_message) {
  std::filesystem::path path = "";

  auto file_picker = xe::ui::FilePicker::Create();
  file_picker->set_mode(ui::FilePicker::Mode::kOpen);
  file_picker->set_type(ui::FilePicker::Type::kFile);
  file_picker->set_multi_selection(false);
  file_picker->set_title(!window_message.empty() ? window_message
                                                 : "Select Content Package");
  file_picker->set_extensions({
      {"Supported Files", "*.iso;*.xex;*.xcp;*.*"},
      {"Disc Image (*.iso)", "*.iso"},
      {"Xbox Executable (*.xex)", "*.xex"},
      {"All Files (*.*)", "*.*"},
  });

  if (file_picker->Show()) {
    auto selected_files = file_picker->selected_files();
    if (!selected_files.empty()) {
      path = selected_files[0];
    }
  }
  return path;
}

bool Emulator::ExceptionCallbackThunk(Exception* ex, void* data) {
  return reinterpret_cast<Emulator*>(data)->ExceptionCallback(ex);
}

bool Emulator::ExceptionCallback(Exception* ex) {
  // Check to see if the exception occurred in guest code.
  auto code_cache = processor()->backend()->code_cache();
  auto code_base = code_cache->execute_base_address();
  auto code_end = code_base + code_cache->total_size();

  if (!processor()->is_debugger_attached() && debugging::IsDebuggerAttached()) {
    // If Xenia's debugger isn't attached but another one is, pass it to that
    // debugger.
    return false;
  } else if (processor()->is_debugger_attached()) {
    // Let the debugger handle this exception. It may decide to continue past
    // it (if it was a stepping breakpoint, etc).
    return processor()->OnUnhandledException(ex);
  }

  if (!(ex->pc() >= code_base && ex->pc() < code_end)) {
    // Didn't occur in guest code. Let it pass.
    return false;
  }

  // Within range. Pause the emulator and eat the exception.
  Pause();

  // Dump information into the log.
  auto current_thread = kernel::XThread::GetCurrentThread();
  assert_not_null(current_thread);

  auto guest_function = code_cache->LookupFunction(ex->pc());
  assert_not_null(guest_function);

  auto context = current_thread->thread_state()->context();

  std::string crash_msg;
  crash_msg.append("==== CRASH DUMP ====\n");
  crash_msg.append(fmt::format("Thread ID (Host: 0x{:08X} / Guest: 0x{:08X})\n",
                               current_thread->thread()->system_id(),
                               current_thread->thread_id()));
  crash_msg.append(
      fmt::format("Thread Handle: 0x{:08X}\n", current_thread->handle()));
  crash_msg.append(
      fmt::format("PC: 0x{:08X}\n",
                  guest_function->MapMachineCodeToGuestAddress(ex->pc())));
  crash_msg.append("Registers:\n");
  for (int i = 0; i < 32; i++) {
    crash_msg.append(fmt::format(" r{:<3} = {:016X}\n", i, context->r[i]));
  }
  for (int i = 0; i < 32; i++) {
    crash_msg.append(fmt::format(" f{:<3} = {:016X} = (double){} = (float){}\n",
                                 i,
                                 *reinterpret_cast<uint64_t*>(&context->f[i]),
                                 context->f[i], *(float*)&context->f[i]));
  }
  for (int i = 0; i < 128; i++) {
    crash_msg.append(
        fmt::format(" v{:<3} = [0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]\n", i,
                    context->v[i].u32[0], context->v[i].u32[1],
                    context->v[i].u32[2], context->v[i].u32[3]));
  }
  XELOGE("{}", crash_msg);
  std::string crash_dlg = fmt::format(
      "The guest has crashed.\n\n"
      "Xenia has now paused itself.\n\n"
      "{}",
      crash_msg);
  // Display a dialog telling the user the guest has crashed.
  if (display_window_ && imgui_drawer_) {
    display_window_->app_context().CallInUIThreadSynchronous([this,
                                                              &crash_dlg]() {
      xe::ui::ImGuiDialog::ShowMessageBox(imgui_drawer_, "Uh-oh!", crash_dlg);
    });
  }

  // Now suspend ourself (we should be a guest thread).
  current_thread->Suspend(nullptr);

  // We should not arrive here!
  assert_always();
  return false;
}

void Emulator::WaitUntilExit() {
  while (true) {
    if (main_thread_) {
      xe::threading::Wait(main_thread_->thread(), false);
    }

    if (restoring_) {
      restore_fence_.Wait();
    } else {
      // Not restoring and the thread exited. We're finished.
      break;
    }
  }

  on_exit();
}

void Emulator::AddGameConfigLoadCallback(GameConfigLoadCallback* callback) {
  assert_not_null(callback);
  // Game config load callbacks handling is entirely in the UI thread.
  assert_true(!display_window_ ||
              display_window_->app_context().IsInUIThread());
  // Check if already added.
  if (std::find(game_config_load_callbacks_.cbegin(),
                game_config_load_callbacks_.cend(),
                callback) != game_config_load_callbacks_.cend()) {
    return;
  }
  game_config_load_callbacks_.push_back(callback);
}

void Emulator::RemoveGameConfigLoadCallback(GameConfigLoadCallback* callback) {
  assert_not_null(callback);
  // Game config load callbacks handling is entirely in the UI thread.
  assert_true(!display_window_ ||
              display_window_->app_context().IsInUIThread());
  auto it = std::find(game_config_load_callbacks_.cbegin(),
                      game_config_load_callbacks_.cend(), callback);
  if (it == game_config_load_callbacks_.cend()) {
    return;
  }
  if (game_config_load_callback_loop_next_index_ != SIZE_MAX) {
    // Actualize the next callback index after the erasure from the vector.
    size_t existing_index =
        size_t(std::distance(game_config_load_callbacks_.cbegin(), it));
    if (game_config_load_callback_loop_next_index_ > existing_index) {
      --game_config_load_callback_loop_next_index_;
    }
  }
  game_config_load_callbacks_.erase(it);
}

std::string Emulator::FindLaunchModule() {
  std::string path("game:\\");

  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");

  if (!xam->loader_data().launch_path.empty()) {
    std::string symbolic_link_path;
    if (kernel_state_->file_system()->FindSymbolicLink(kDefaultGameSymbolicLink,
                                                       symbolic_link_path)) {
      std::filesystem::path file_path = symbolic_link_path;
      // Remove previous symbolic links.
      // Some titles can provide root within specific directory.
      kernel_state_->file_system()->UnregisterSymbolicLink(
          kDefaultPartitionSymbolicLink);
      kernel_state_->file_system()->UnregisterSymbolicLink(
          kDefaultGameSymbolicLink);

      file_path /= std::filesystem::path(xam->loader_data().launch_path);

      kernel_state_->file_system()->RegisterSymbolicLink(
          kDefaultPartitionSymbolicLink,
          xe::path_to_utf8(file_path.parent_path()));
      kernel_state_->file_system()->RegisterSymbolicLink(
          kDefaultGameSymbolicLink, xe::path_to_utf8(file_path.parent_path()));

      return xe::path_to_utf8(file_path);
    }
  }

  if (!cvars::launch_module.empty()) {
    return path + cvars::launch_module;
  }

  std::string default_module("default.xex");

  auto gameinfo_entry(file_system_->ResolvePath(path + "GameInfo.bin"));
  if (gameinfo_entry) {
    vfs::File* file = nullptr;
    X_STATUS result =
        gameinfo_entry->Open(vfs::FileAccess::kGenericRead, &file);
    if (XSUCCEEDED(result)) {
      std::vector<uint8_t> buffer(gameinfo_entry->size());
      size_t bytes_read = 0;
      result = file->ReadSync(buffer.data(), buffer.size(), 0, &bytes_read);
      if (XSUCCEEDED(result)) {
        kernel::util::GameInfo info(buffer);
        if (info.is_valid()) {
          XELOGI("Found virtual title {}", info.virtual_title_id());

          const std::string xna_id("584E07D1");
          auto xna_id_entry(file_system_->ResolvePath(path + xna_id));
          if (xna_id_entry) {
            default_module = xna_id + "\\" + info.module_name();
          } else {
            XELOGE("Could not find fixed XNA path {}", xna_id);
          }
        }
      }
    }
  }

  return path + default_module;
}

static std::string format_version(xex2_version version) {
  // fmt::format doesn't like bit fields
  uint32_t major, minor, build, qfe;
  major = version.major;
  minor = version.minor;
  build = version.build;
  qfe = version.qfe;
  if (qfe) {
    return fmt::format("{}.{}.{}.{}", major, minor, build, qfe);
  }
  if (build) {
    return fmt::format("{}.{}.{}", major, minor, build);
  }
  return fmt::format("{}.{}", major, minor);
}

X_STATUS Emulator::CompleteLaunch(const std::filesystem::path& path,
                                  const std::string_view module_path) {
  // Making changes to the UI (setting the icon) and executing game config
  // load callbacks which expect to be called from the UI thread.
  assert_true(display_window_->app_context().IsInUIThread());

  // Setup NullDevices for raw HDD partition accesses
  // Cache/STFC code baked into games tries reading/writing to these
  // By using a NullDevice that just returns success to all IO requests it
  // should allow games to believe cache/raw disk was accessed successfully

  // NOTE: this should probably be moved to xenia_main.cc, but right now we
  // need to register the \Device\Harddisk0\ NullDevice _after_ the
  // \Device\Harddisk0\Partition1 HostPathDevice, otherwise requests to
  // Partition1 will go to this. Registering during CompleteLaunch allows us
  // to make sure any HostPathDevices are ready beforehand. (see comment above
  // cache:\ device registration for more info about why)
  auto null_paths = {std::string("\\Partition0"), std::string("\\Cache0"),
                     std::string("\\Cache1")};
  auto null_device =
      std::make_unique<vfs::NullDevice>("\\Device\\Harddisk0", null_paths);
  if (null_device->Initialize()) {
    file_system_->RegisterDevice(std::move(null_device));
  }

  // Reset state.
  title_id_ = std::nullopt;
  title_name_ = "";
  title_version_ = "";
  display_window_->SetIcon(nullptr, 0);

  // Allow xam to request module loads.
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");

  XELOGI("Loading module {}", module_path);
  auto module = kernel_state_->LoadUserModule(module_path);
  if (!module) {
    XELOGE("Failed to load user module {}", xe::path_to_utf8(path));
    return X_STATUS_NOT_FOUND;
  }

  X_RESULT result = kernel_state_->ApplyTitleUpdate(module);
  if (XFAILED(result)) {
    XELOGE("Failed to apply title update! Cannot run module {}",
           xe::path_to_utf8(path));
    return result;
  }

  result = kernel_state_->FinishLoadingUserModule(module);
  if (XFAILED(result)) {
    XELOGE("Failed to initialize user module {}", xe::path_to_utf8(path));
    return result;
  }
  // Grab the current title ID.
  xex2_opt_execution_info* info = nullptr;
  uint32_t workspace_address = 0;
  module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &info);

  kernel_state_->memory()
      ->LookupHeapByType(false, 0x1000)
      ->Alloc(module->workspace_size(), 0x1000,
              kMemoryAllocationReserve | kMemoryAllocationCommit,
              kMemoryProtectRead | kMemoryProtectWrite, false,
              &workspace_address);

  if (!info) {
    title_id_ = 0;
  } else {
    title_id_ = info->title_id;
    auto title_version = info->version();
    if (title_version.value != 0) {
      title_version_ = format_version(title_version);
    }
  }

  // Try and load the resource database (xex only).
  if (module->title_id()) {
    auto title_id = fmt::format("{:08X}", module->title_id());

    // Load the per-game configuration file and make sure updates are handled
    // by the callbacks.
    config::LoadGameConfig(title_id);
    assert_true(game_config_load_callback_loop_next_index_ == SIZE_MAX);
    game_config_load_callback_loop_next_index_ = 0;
    while (game_config_load_callback_loop_next_index_ <
           game_config_load_callbacks_.size()) {
      game_config_load_callbacks_[game_config_load_callback_loop_next_index_++]
          ->PostGameConfigLoad();
    }
    game_config_load_callback_loop_next_index_ = SIZE_MAX;

    const kernel::util::XdbfGameData db = kernel_state_->module_xdbf(module);

    game_info_database_ = std::make_unique<kernel::util::GameInfoDatabase>(&db);

    if (game_info_database_->IsValid()) {
      title_name_ = game_info_database_->GetTitleName(
          static_cast<XLanguage>(cvars::user_language));
      XELOGI("Title name: {}", title_name_);

      // Show achievments data
      tabulate::Table table;
      table.format().multi_byte_characters(true);
      table.add_row({"ID", "Title", "Description", "Gamerscore"});

      const std::vector<kernel::util::GameInfoDatabase::Achievement>
          achievement_list = game_info_database_->GetAchievements();
      for (const kernel::util::GameInfoDatabase::Achievement& entry :
           achievement_list) {
        table.add_row({fmt::format("{}", entry.id), entry.label,
                       entry.description, fmt::format("{}", entry.gamerscore)});
      }
      XELOGI("-------------------- ACHIEVEMENTS --------------------\n{}",
             table.str());

      const std::vector<kernel::util::GameInfoDatabase::Property>
          properties_list = game_info_database_->GetProperties();

      table = tabulate::Table();
      table.format().multi_byte_characters(true);
      table.add_row({"ID", "Name", "Data Size"});

      for (const kernel::util::GameInfoDatabase::Property& entry :
           properties_list) {
        std::string label =
            string_util::remove_eol(string_util::trim(entry.description));
        table.add_row({fmt::format("{:08X}", entry.id), label,
                       fmt::format("{}", entry.data_size)});
      }
      XELOGI("-------------------- PROPERTIES --------------------\n{}",
             table.str());

      const std::vector<kernel::util::GameInfoDatabase::Context> contexts_list =
          game_info_database_->GetContexts();

      table = tabulate::Table();
      table.format().multi_byte_characters(true);
      table.add_row({"ID", "Name", "Default Value", "Max Value"});

      for (const kernel::util::GameInfoDatabase::Context& entry :
           contexts_list) {
        std::string label =
            string_util::remove_eol(string_util::trim(entry.description));
        table.add_row({fmt::format("{:08X}", entry.id), label,
                       fmt::format("{}", entry.default_value),
                       fmt::format("{}", entry.max_value)});
      }
      XELOGI("-------------------- CONTEXTS --------------------\n{}",
             table.str());

      auto icon_block = game_info_database_->GetIcon();
      if (!icon_block.empty()) {
        display_window_->SetIcon(icon_block.data(), icon_block.size());
      }
    }
  }

  auto patch_addr = [module](uint32_t addr, uint32_t value) {
    auto* patch_ptr =
        (xe::be<uint32_t>*)module->memory()->TranslateVirtual(addr);
    auto heap = module->memory()->LookupHeap(addr);

    uint32_t old_protect = 0;
    heap->Protect(addr, 4, kMemoryProtectRead | kMemoryProtectWrite,
                  &old_protect);
    *patch_ptr = value;
    heap->Protect(addr, 4, old_protect);
  };

  if (module->title_id() == 0x584109C2) {
    // Prevent game from writing RS thumbstick to crosshair/gun position
    // Multiple PD revisions so we'll need to search the code...

    std::vector<uint32_t> search_insns = {
        0xD17F16A8,  // stfs      f11, 0x16A8(r31)
        0xD19F16A4,  // stfs      f12, 0x16A4(r31)
        0xD19F1690,  // stfs      f12, 0x1690(r31)
        0xD15F1694,  // stfs      f10, 0x1694(r31)
        0xD0FF0CFC,  // stfs      f7, 0xCFC(r31)
        0xD0BF0D00   // stfs      f5, 0xD00(r31)
    };

    int patched = 0;

    auto* xex = module->xex_module();
    auto* check_addr = (xe::be<uint32_t>*)module->memory()->TranslateVirtual(
        xex->base_address());
    auto* end_addr = (xe::be<uint32_t>*)module->memory()->TranslateVirtual(
        xex->base_address() + xex->image_size());

    while (end_addr > check_addr) {
      auto value = *check_addr;

      for (auto test : search_insns) {
        if (test == value) {
          uint32_t addr = module->memory()->HostToGuestVirtual(check_addr);
          patch_addr(addr, 0x60000000);
          patched++;
          break;
        }
      }

      check_addr++;
    }
  }

  if (module->title_id() == 0x584108A9) {
    struct GEPatchOffsets {
      uint32_t check_addr;
      uint32_t check_value;

      uint32_t crosshair_addr1;
      uint32_t crosshair_patch1;
      uint32_t crosshair_addr2;
      uint32_t crosshair_patch2;

      uint32_t returnarcade_addr1;
      uint32_t returnarcade_patch1;
      uint32_t returnarcade_addr2;
      uint32_t returnarcade_patch2;
      uint32_t returnarcade_addr3;
      uint32_t returnarcade_patch3;

      uint32_t blur_addr;
      uint32_t debug_addr;
    };

    std::vector<GEPatchOffsets> supported_builds = {
        // Nov 2007 Release build
        {0x8200336C, 0x676f6c64, 0x820A45D0, 0x4800003C, 0x820A46D4, 0x4800003C,
         0x820F7750, 0x2F1E0007, 0x820F7D04, 0x2F1A0007, 0x820F7780, 0x2B0A0003,
         0x82188E70, 0x82189F28},

        // Nov 2007 Team build
        {0x82003398, 0x676f6c64, 0x820C85B0, 0x480000B0, 0x820C88B8, 0x480000B0,
         0x8213ABE8, 0x2F0B0007, 0x8213AF0C, 0x2F0B0007, 0x8213ACB4, 0x2B0B0004,
         0x8221DF34, 0},

        // Nov 2007 Debug build
        {0x82005540, 0x676f6c64, 0x822A2BFC, 0x480000B0, 0x822A2F04, 0x480000B0,
         0x82344D04, 0x2F0B0007, 0x82345030, 0x2F0B0007, 0x82344DD0, 0x2B0B0004,
         0x824AB510, 0},
    };

    for (auto& build : supported_builds) {
      auto* test_addr = (xe::be<uint32_t>*)module->memory()->TranslateVirtual(
          build.check_addr);
      if (*test_addr != build.check_value) {
        continue;
      }

      // Prevent game from overwriting crosshair/gun positions
      if (build.crosshair_addr1) {
        patch_addr(build.crosshair_addr1, build.crosshair_patch1);
      }
      if (build.crosshair_addr2) {
        patch_addr(build.crosshair_addr2, build.crosshair_patch2);
      }

      // Hide "return to arcade" menu option
      if (build.returnarcade_addr1) {
        patch_addr(build.returnarcade_addr1, build.returnarcade_patch1);
      }
      if (build.returnarcade_addr2) {
        patch_addr(build.returnarcade_addr2, build.returnarcade_patch2);
      }
      // Prevent "return to arcade" code from being executed
      if (build.returnarcade_addr3) {
        patch_addr(build.returnarcade_addr3, build.returnarcade_patch3);
      }

      if (cvars::ge_remove_blur && build.blur_addr) {
        // Patch out N64 blur
        // Source:
        // https://github.com/xenia-canary/game-patches/blob/main/patches/584108A9.patch

        patch_addr(build.blur_addr, 0x60000000);
      }

      if (cvars::ge_debug_menu && build.debug_addr) {
        // Enable debug menu
        patch_addr(build.debug_addr, 0x2B0B0000);
      }

      break;
    }
  }

  if (module->title_id() == 0x545107FC) {
    struct SR2PatchOffsets {
      uint32_t check_addr;
      uint32_t check_value;

      uint32_t beNOP;
      uint32_t multiplierwrite_addr1;
      uint32_t multiplierwrite_addr2;
      uint32_t multiplierwrite_addr3;
      uint32_t multiplierwrite_addr4;

      uint32_t zero_patch1;  // Not Exactly zero but 0.001f otherwise it'll
                             // break interiors - Clippy95
      uint32_t sensYwrite_addr1;
      uint32_t sensYwrite_addr2;
      uint32_t sensYwrite_addr3;
      uint32_t sensYwrite_addr4;
      uint32_t sensYwrite_addr5;
      uint32_t sensYwrite_addr6;
      uint32_t sensYwrite_addr7;

      uint32_t sensXwrite_addr1;
      uint32_t sensXwrite_addr2;
      uint32_t sensXwrite_addr3;
      uint32_t sensXwrite_addr4;
      uint32_t sensXwrite_addr5;
      uint32_t sensXwrite_addr6;
      uint32_t sensXwrite_addr7;

      uint32_t sensYvalue_addr1;
      uint32_t sensXvalue_addr2;

      uint32_t multiplierread_addr1;
      uint32_t multiplierread_addr2;
      uint32_t multiplierread_addr3;
      uint32_t multiplierread_addr4;
      uint32_t multiplierread_addr5;

      uint32_t sensYwrite_addr8;
      uint32_t sensXwrite_addr8;
      uint32_t Vehicle_RotationXWrite_addr1;
      uint32_t Vehicle_RotationXWrite_addr2;  // Handbrake.
      uint32_t aim_assist_xbtl;  // File declares aim_assist values.
    };

    std::vector<SR2PatchOffsets> supported_builds = {
        // TU3 Release build
        {0x82014390, 0x3d088889, 0x60000000, 0x8219f5b8, 0x8219f5cc, 0x8219f61c,
         0x8219f5f4, 0x38d1b717, 0x824788fc, 0x824e7f58, 0x824e6ac8, 0x82478090,
         0x8247832c, 0x821a4b84, 0x824e6a68, 0x824e7f50, 0x824e6b8c, 0x82478934,
         0x824e6b2c, 0x82478330, 0x82478094, 0x821a4b88, 0x82B7A5AC, 0x82B7A5A8,
         0x82B77C04, 0x82B77C08, 0x82B77C0C, 0x82B77C08, 0x82B77C10, 0x821A4D20,
         0x821A4D18, 0x821a1f74, 0x821A2A2C, 0x820A61C0},
    };

    for (auto& build : supported_builds) {
      auto* test_addr = (xe::be<uint32_t>*)module->memory()->TranslateVirtual(
          build.check_addr);
      if (*test_addr != build.check_value) {
        continue;
      }

      // Write beNOP to each write address
      patch_addr(build.multiplierwrite_addr1, build.beNOP);
      patch_addr(build.multiplierwrite_addr2, build.beNOP);
      patch_addr(build.multiplierwrite_addr3, build.beNOP);
      patch_addr(build.multiplierwrite_addr4, build.beNOP);
      patch_addr(build.sensYwrite_addr1, build.beNOP);
      patch_addr(build.sensYwrite_addr2, build.beNOP);
      patch_addr(build.sensYwrite_addr3, build.beNOP);
      patch_addr(build.sensYwrite_addr4, build.beNOP);
      patch_addr(build.sensYwrite_addr5, build.beNOP);
      patch_addr(build.sensYwrite_addr6, build.beNOP);
      patch_addr(build.sensYwrite_addr7, build.beNOP);
      patch_addr(build.sensYwrite_addr8, build.beNOP);
      patch_addr(build.sensXwrite_addr1, build.beNOP);
      patch_addr(build.sensXwrite_addr2, build.beNOP);
      patch_addr(build.sensXwrite_addr3, build.beNOP);
      patch_addr(build.sensXwrite_addr4, build.beNOP);
      patch_addr(build.sensXwrite_addr5, build.beNOP);
      patch_addr(build.sensXwrite_addr6, build.beNOP);
      patch_addr(build.sensXwrite_addr7, build.beNOP);
      patch_addr(build.sensXwrite_addr8, build.beNOP);

      // Write zero_patch1 to each read and sens value address
      patch_addr(build.multiplierread_addr1, build.zero_patch1);
      patch_addr(build.multiplierread_addr2, build.zero_patch1);
      patch_addr(build.multiplierread_addr3, build.zero_patch1);
      patch_addr(build.multiplierread_addr4, build.zero_patch1);
      patch_addr(build.multiplierread_addr5, build.zero_patch1);
      patch_addr(build.sensYvalue_addr1, build.zero_patch1);
      patch_addr(build.sensXvalue_addr2, build.zero_patch1);
      if (cvars::sr2_better_drive_cam && build.Vehicle_RotationXWrite_addr1) {
        patch_addr(build.Vehicle_RotationXWrite_addr1, build.beNOP);
      }

      if (cvars::sr2_better_handbrake_cam &&
          build.Vehicle_RotationXWrite_addr2) {
        patch_addr(build.Vehicle_RotationXWrite_addr2, build.beNOP);
      }
      if (cvars::disable_autoaim && build.aim_assist_xbtl) {
        patch_addr(build.aim_assist_xbtl, build.beNOP);
      }

      break;
    }
  }

  if (module->title_id() == 0x5454082B) {
    struct RDRPatchOffsets {
      uint32_t check_addr;
      uint32_t check_value;
      uint32_t BENop;
      uint32_t BEStub;
      uint32_t auto_center_read_address;  // We can only move the camera values
                                          // on foot/horse if the in-game auto
                                          // center option is disabled.
      uint32_t aim_assist_function_address;
      uint32_t alt_auto_center_read_address;  // Most likely either horz or vert
    };
    std::vector<RDRPatchOffsets> supported_builds = {
        // RDR GOTY DISK 1
        {0x82010BEC, 0x7A3A5C72, 0x60000000, 0x4e800020, 0x82371E78, 0x822F9E60,
         0x82371E58},
        // RDR GOTY DISK 2
        {0x82010C0C, 0x7A3A5C72, 0x60000000, 0x4e800020, 0x82371E58, 0x822F9F60,
         NULL},
        // RDR TU0
        {0x8201071C, 0x7A3A5C72, 0x60000000, 0x4e800020, 0x82370C08, 0x822F83B0,
         0x82370C28},
        // RDR TU9
        {0x82010C1C, 0x7A3A5C72, 0x60000000, 0x4e800020, 0x823717D8, 0x822F97C8,
         0x823717FC},
        // RDR Undead Nightmare Standalone TU4 #5B48AF70
        {0x82010B9C, 0x7A3A5C72, 0x60000000, 0x4e800020, 0x82371C80, 0x822D1690,
         0x82371CA0}};
    for (auto& build : supported_builds) {
      auto* test_addr = (xe::be<uint32_t>*)module->memory()->TranslateVirtual(
          build.check_addr);
      if (*test_addr != build.check_value) {
        continue;
      }
      patch_addr(build.auto_center_read_address, build.BENop);
      if (build.alt_auto_center_read_address)
        patch_addr(build.alt_auto_center_read_address, build.BENop);
      if (cvars::disable_autoaim && build.aim_assist_function_address) {
        patch_addr(build.aim_assist_function_address, build.BEStub);
      }
    }
  }

  const uint32_t kTitleIdCODGhostsDEV = 0x4156088E;
  const uint32_t kTitleIdCODNX1 = 0x4156089E;
  const uint32_t kTitleIdCODBO2 = 0x415608C3;
  const uint32_t kTitleIdCODMW3 = 0x415608CB;
  const uint32_t kTitleIdCODMW2 = 0x41560817;
  const uint32_t kTitleIdCODWaW = 0x4156081C;
  const uint32_t kTitleIdCOD4 = 0x415607E6;
  const uint32_t kTitleIdCOD3 = 0x415607E1;
  if (cvars::disable_autoaim) {
    if (module->title_id() == kTitleIdCOD4 ||
        module->title_id() == kTitleIdCODMW2 ||
        module->title_id() == kTitleIdCODMW3 ||
        module->title_id() == kTitleIdCODBO2 ||
        module->title_id() == kTitleIdCODNX1 ||
        module->title_id() == kTitleIdCOD3 ||
        module->title_id() == kTitleIdCODWaW ||
        module->title_id() == kTitleIdCODGhostsDEV) {
      struct CODPatchOffsets {
        uint32_t cg_fov_address;
        uint32_t cg_fov;
        uint32_t
            lockon_address;  // Usually this is AimAssist_ApplyLockOn /
                             // AimAssist_UpdateLockOn, thanks to Andersson799,
                             // this doesn't disable Aim Assist in SP, which be
                             // can be disabled in the options.
        uint8_t patch_type;  // 0: 4E800020, 1: 60000000
      };

      std::vector<CODPatchOffsets> supported_builds = {
          // Call of Duty 4 SP
          {0x82044468, 0x63675F66, 0x82308D68, 0},

          // Call of Duty 4 TU0 MP
          {0x82BAD56C, 0x63675F66, 0x8233F508, 0},

          // Call of Duty 4 TU4 MP
          {0x82051048, 0x63675F66, 0x82347D58, 0},

          // Call of Duty 4 Alpha 253 SP
          {0x8204EB24, 0x63675F66, 0x820924f8, 0},

          // Call of Duty 4 Alpha 253 SP exe
          {0x8200EAA4, 0x63675F66, 0x820f2a78, 0},

          // Call of Duty 4 Alpha 253 MP
          {0x82055EF4, 0x63675F66, 0x820a2558, 0},

          // Call of Duty 4 Alpha 253 MP exe
          {0x82011EF4, 0x63675F66, 0x821432a8, 0},

          // Call of Duty 4 Alpha 270 SP
          {0x8204E7FC, 0x63675F66, 0x820A21F0, 0},

          // Call of Duty 4 Alpha 270 SP exe
          {0x8200E4FC, 0x63675F66, 0x820f2ad8, 0},

          // Call of Duty 4 Alpha 270 MP
          {0x8205617C, 0x63675F66, 0x820a21e8, 0},

          // Call of Duty 4 Alpha 270 MP exe
          {0x82012114, 0x63675F66, 0x82143380, 0},

          // Call of Duty 4 Alpha 290 SP
          {0x8203ABE8, 0x63675F66, 0x82082390, 0},

          // Call of Duty 4 Alpha 290 SP exe
          {0x8200E9EC, 0x63675F66, 0x820e2d00, 0},

          // Call of Duty 4 Alpha 290 MP
          {0x82042588, 0x63675F66, 0x82092398, 0},

          // Call of Duty 4 Alpha 290 MP exe
          {0x82012624, 0x63675F66, 0x82143668, 0},

          // Call of Duty 4 Alpha 328 SP
          {0x82009C80, 0x63675F66, 0x820eb690, 0},

          // Call of Duty 4 Alpha 328 SP exe
          {0x8200EB58, 0x63675F66, 0x82103140, 0},

          // Call of Duty 4 Alpha 328 MP
          {0x8200BB2C, 0x63675F66, 0x820fb770, 0},

          // Call of Duty 4 Alpha 328 MP exe
          {0x82012664, 0x63675F66, 0x82143518, 0},

          // Call of Duty MW2 Alpha 482 SP
          {0x82007560, 0x63675F66, 0x820d7828, 0},

          // Call of Duty MW2 Alpha 482 MP
          {0x8200FF48, 0x63675F66, 0x820f5f98, 0},

          // Call of Duty MW2 TU0 SP
          {0x82020954, 0x63675F66, 0x820D7838, 0},
          // I found COD3's Aim Assist thanks to Garungorp's Mouse Injector
          // https://github.com/garungorp/MouseInjectorDolphinDuck/blob/e9af92296038f82968a222a7eb2aef88b8d18c82/games/ps2_cod3.c#L28
          // Call of Duty 3 SP TU0
          {0x8248C6D4, 0xC0C70008, 0x8248DE8C, 1},

          // Call of Duty 3 SP TU3
          {0x8248C6D4, 0x38210160, 0x8248D43C, 1},

          // Call of Duty 3 MP TU0
          {0x82078614, 0x63675F66, 0x824AC7A0, 1},

          // Call of Duty 3 MP TU3
          {0x8206E994, 0x63675F66, 0x82471D70, 1},

          // New Moon Patched XEX (Black Ops 2 Alpha)
          {0x82004860, 0x63675F66, 0x82137D50, 0},

          // Call of Duty MW3 TU0 MP
          {0x8200C558, 0x63675F66, 0x820D4710, 0},

          // Call of Duty MW2 TU0 MP
          {0x820102D8, 0x63675F66, 0x820F5FB0, 0},

          // Call of Duty NX1 Nightly SP Maps
          {0x82021104, 0x63675F66, 0x820F9390, 0},

          // Call of Duty NX1 SP
          {0x8200FC1C, 0x63675F66, 0x82183d90, 0},

          // Call of Duty NX1 MP Demo
          {0x82012228, 0x63675F66, 0x820F9310, 0},

          // Call of Duty NX1 MP
          {0x8201E584, 0x63675F66, 0x821d5180, 0},

          // Call of Duty NX1 Nightly MP Maps
          {0x8201DD04, 0x63675F66, 0x821c7a68, 0},

          // Call Of Duty World At War TU7 SP
          {0x82055874, 0x63675F66, 0x820E1E50, 0},

          // Call Of Duty World At War TU7 MP
          {0x82012704, 0x63675F66, 0x82124C10, 0},
          // CallOfDutyGhosts_IW6_DEV_2iw6mp
          {0x820BB320, 0x63675F66, 0x82293F50, 0},
          // CallOfDutyGhosts_IW6_DEV_1iw6sp
          {0x82032648, 0x63675F66, 0x82224b10, 0},
      };

      for (auto& build : supported_builds) {
        auto* fov_addr = (xe::be<uint32_t>*)module->memory()->TranslateVirtual(
            build.cg_fov_address);
        if (*fov_addr != build.cg_fov) {
          continue;
        }

        if (build.lockon_address) {
          uint32_t patch_value =
              (build.patch_type == 0) ? 0x4E800020 : 0x60000000;
          (build.patch_type == 0) ? 0x4E800020 : 0x60000000;
          patch_addr(build.lockon_address, patch_value);
        }

        break;
      }
    }
  }

  // Initializing the shader storage in a blocking way so the user doesn't
  // miss the initial seconds - for instance, sound from an intro video may
  // start playing before the video can be seen if doing this in parallel with
  // the main thread.
  on_shader_storage_initialization(true);
  graphics_system_->InitializeShaderStorage(cache_root_, title_id_.value(),
                                            true);
  on_shader_storage_initialization(false);

  auto main_thread = kernel_state_->LaunchModule(module);
  if (!main_thread) {
    return X_STATUS_UNSUCCESSFUL;
  }
  main_thread_ = main_thread;
  on_launch(title_id_.value(), title_name_);

  input_system()->UpdateTitleId(title_id_.value());

  // Plugins must be loaded after calling LaunchModule() and
  // FinishLoadingUserModule() which will apply TUs and patching to the main
  // xex.
  if (cvars::allow_plugins) {
    if (plugin_loader_->IsAnyPluginForTitleAvailable(title_id_.value(),
                                                     module->hash().value())) {
      plugin_loader_->LoadTitlePlugins(title_id_.value());
    }
  }

  return X_STATUS_SUCCESS;
}

}  // namespace xe
