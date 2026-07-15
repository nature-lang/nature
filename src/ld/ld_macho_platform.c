#include "ld_macho_platform.h"

#include <string.h>

/* Platform matching follows Zig's MachO.Platform.fromLoadCommand/eqlTarget
   behavior at 738d2be9d6b6ef3ff3559130c05159ef53336224.  Nature currently emits
   macOS executables only, so every other Apple platform/ABI is rejected even
   though it shares the arm64 CPU type. */

static const char *ld_macho_platform_name(uint32_t platform) {
    switch (platform) {
        case LD_PLATFORM_MACOS:
            return "macOS";
        case LD_PLATFORM_IOS:
            return "iOS";
        case LD_PLATFORM_TVOS:
            return "tvOS";
        case LD_PLATFORM_WATCHOS:
            return "watchOS";
        case LD_PLATFORM_BRIDGEOS:
            return "bridgeOS";
        case LD_PLATFORM_MACCATALYST:
            return "Mac Catalyst";
        case LD_PLATFORM_IOSSIMULATOR:
            return "iOS simulator";
        case LD_PLATFORM_TVOSSIMULATOR:
            return "tvOS simulator";
        case LD_PLATFORM_WATCHOSSIMULATOR:
            return "watchOS simulator";
        case LD_PLATFORM_DRIVERKIT:
            return "DriverKit";
        case LD_PLATFORM_VISIONOS:
            return "visionOS";
        case LD_PLATFORM_VISIONOSSIMULATOR:
            return "visionOS simulator";
        default:
            return "unknown Apple platform";
    }
}

static int ld_macho_platform_record(ld_context_t *ctx,
                                    const char *input_name,
                                    ld_macho_platform_info_t *info,
                                    uint32_t platform,
                                    uint32_t minimum_version,
                                    uint32_t sdk_version) {
    if (platform != LD_PLATFORM_MACOS) {
        return ld_fail(ctx, LD_UNSUPPORTED,
                       "Mach-O input '%s' targets %s (platform %u), not macOS",
                       input_name, ld_macho_platform_name(platform), platform);
    }
    if (info->seen && info->platform != platform) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "Mach-O input '%s' contains conflicting platform load commands",
                       input_name);
    }
    info->seen = true;
    info->platform = platform;
    info->minimum_version = minimum_version;
    info->sdk_version = sdk_version;
    return LD_OK;
}

int ld_macho_platform_parse_command(ld_context_t *ctx, const char *input_name,
                                    const uint8_t *command_bytes,
                                    size_t command_size,
                                    ld_macho_platform_info_t *info,
                                    bool *recognized) {
    if (!ctx || !input_name || !command_bytes || !info || !recognized ||
        command_size < sizeof(ld_load_command_t)) {
        return ld_fail(ctx, LD_INVALID_ARGUMENT,
                       "invalid Mach-O platform command parser arguments");
    }
    *recognized = false;
    ld_load_command_t command;
    memcpy(&command, command_bytes, sizeof(command));
    if (command.cmd == LD_LC_BUILD_VERSION) {
        *recognized = true;
        if (command_size < sizeof(ld_build_version_command_t) ||
            command.cmdsize < sizeof(ld_build_version_command_t) ||
            command.cmdsize > command_size) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "invalid LC_BUILD_VERSION in '%s'", input_name);
        }
        ld_build_version_command_t build;
        memcpy(&build, command_bytes, sizeof(build));
        uint64_t tool_bytes = (uint64_t) build.ntools *
                              sizeof(ld_build_tool_version_t);
        if (tool_bytes > command.cmdsize - sizeof(build)) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "invalid LC_BUILD_VERSION tool list in '%s'",
                           input_name);
        }
        return ld_macho_platform_record(ctx, input_name, info, build.platform,
                                        build.minos, build.sdk);
    }

    uint32_t platform;
    switch (command.cmd) {
        case LD_LC_VERSION_MIN_MACOSX:
            platform = LD_PLATFORM_MACOS;
            break;
        case LD_LC_VERSION_MIN_IPHONEOS:
            platform = LD_PLATFORM_IOS;
            break;
        case LD_LC_VERSION_MIN_TVOS:
            platform = LD_PLATFORM_TVOS;
            break;
        case LD_LC_VERSION_MIN_WATCHOS:
            platform = LD_PLATFORM_WATCHOS;
            break;
        default:
            return LD_OK;
    }
    *recognized = true;
    if (command_size < sizeof(ld_version_min_command_t) ||
        command.cmdsize < sizeof(ld_version_min_command_t) ||
        command.cmdsize > command_size) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "invalid legacy platform load command in '%s'",
                       input_name);
    }
    ld_version_min_command_t version;
    memcpy(&version, command_bytes, sizeof(version));
    return ld_macho_platform_record(ctx, input_name, info, platform,
                                    version.version, version.sdk);
}
