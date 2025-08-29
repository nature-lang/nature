const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{});

    const opt_runtime = b.option(bool, "runtime", "build nature runtime") orelse false;

    const os = target.result.os.tag;
    const arch = target.result.cpu.arch;

    const lib_path_os = switch (os) {
        .macos => "darwin",
        .linux => "linux",
        else => "unknown",
    };
    const lib_path_arch = switch (arch) {
        .x86_64 => "amd64",
        .aarch64 => "arm64",
        .riscv64 => "riscv64",
        else => "unknown",
    };

    var buffer: [256]u8 = undefined;
    const lib_path = std.fmt.bufPrint(&buffer, "lib/{s}_{s}", .{ lib_path_os, lib_path_arch }) catch unreachable;

    const exe = b.addExecutable(.{
        .name = "nature",
        .root_source_file = null,
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    const config_header = b.addConfigHeader(.{
        .include_path = "config/config.h",
    }, .{
        .BUILD_VERSION = blk: {
            const file = try std.fs.cwd().openFile("VERSION", .{});
            defer file.close();
            const version_content = try file.readToEndAlloc(b.allocator, 1024);
            break :blk std.mem.trim(u8, version_content, "\n ");
        },
        .BUILD_TIME = std.time.timestamp(),
        .BUILD_TYPE = if (optimize == .Debug) "debug" else "release",
    });
    std.debug.print("std.time.timestamp(): {}", .{std.time.timestamp()});
    exe.addConfigHeader(config_header);
    if (opt_runtime) {
        const lib = try buildRuntime(b, target, optimize, lib_path);
        exe.linkLibrary(lib);
    } else {
        // cp static library to install path
        const allocator = std.heap.page_allocator;

        const full_path = try std.fs.path.join(allocator, &[_][]const u8{ lib_path, "/libruntime.a" });
        defer allocator.free(full_path);

        const copy_step = b.addSystemCommand(&.{
            "cp",
            b.getInstallPath(.lib, "libruntime.a"),
            full_path,
        });
        b.getInstallStep().dependOn(&copy_step.step);

        exe.addLibraryPath(b.path(lib_path));
        exe.linkSystemLibrary("runtime");
    }

    exe.addIncludePath(.{ .cwd_relative = "./" });
    exe.addIncludePath(.{ .cwd_relative = "./include/" });

    setCMacros(exe, target);

    const nature_main = &.{"main.c"};
    const nature_src = try findCFiles(b.allocator, "src");
    defer {
        for (nature_src.items) |path| b.allocator.free(path);
        nature_src.deinit();
    }

    exe.addCSourceFiles(.{ .files = nature_main, .flags = &.{"-std=gnu11"} });
    exe.addCSourceFiles(.{ .files = nature_src.items, .flags = &.{} });

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);

    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}

pub fn buildRuntime(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lib_path: []const u8,
) !*std.Build.Step.Compile {
    const mod = b.createModule(.{
        .root_source_file = null,
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    const lib = b.addStaticLibrary(.{
        .name = "runtime",
        .root_module = mod,
    });

    // Include the source files for the runtime library.
    const aco = &.{
        "runtime/aco/aco.c",
        "runtime/aco/acosw.S",
    };

    setCMacros(lib, target);

    lib.addIncludePath(.{ .cwd_relative = "./" });
    lib.addIncludePath(.{ .cwd_relative = "./include/" });

    const nature_utils = try findCFiles(b.allocator, "utils");
    defer {
        for (nature_utils.items) |path| b.allocator.free(path);
        nature_utils.deinit();
    }
    lib.addCSourceFiles(.{ .files = aco, .flags = &.{"-std=gnu11"} });
    lib.addCSourceFiles(.{ .files = nature_utils.items, .flags = &.{"-std=gnu11"} });

    const runtime_files = try findCFiles(b.allocator, "runtime");
    defer {
        for (runtime_files.items) |path| b.allocator.free(path);
        runtime_files.deinit();
    }
    lib.addCSourceFiles(.{ .files = runtime_files.items, .flags = &.{"-std=gnu11"} });

    lib.addLibraryPath(b.path(lib_path));

    // Install the runtime library.
    b.installArtifact(lib);

    return lib;
}
pub fn findCFiles(allocator: std.mem.Allocator, dir_path: []const u8) !std.ArrayList([]const u8) {
    var result = std.ArrayList([]const u8).init(allocator);
    errdefer result.deinit();

    try recursiveFindCFiles(allocator, dir_path, &result);
    return result;
}

fn recursiveFindCFiles(
    allocator: std.mem.Allocator,
    base_dir: []const u8,
    result: *std.ArrayList([]const u8),
) !void {
    var dir = std.fs.cwd().openDir(base_dir, .{ .iterate = true }) catch |err| {
        std.log.debug(" Failed to open directory '{s}': {s}", .{ base_dir, @errorName(err) });
        return;
    };
    defer dir.close();

    var iter = dir.iterate();
    while (try iter.next()) |entry| {
        const full_path = try std.fs.path.join(allocator, &.{ base_dir, entry.name });
        defer allocator.free(full_path);

        switch (entry.kind) {
            .file => if (std.mem.endsWith(u8, entry.name, ".c")) {
                const file_path = try allocator.dupe(u8, full_path);
                errdefer allocator.free(file_path);
                try result.append(file_path);
            },
            .directory => try recursiveFindCFiles(allocator, full_path, result),
            else => {},
        }
    }
}

fn setCMacros(compile: *std.Build.Step.Compile, target: std.Build.ResolvedTarget) void {
    const os = target.result.os.tag;
    const arch = target.result.cpu.arch;
    switch (os) {
        .macos => {
            compile.root_module.addCMacro("__DARWIN", "1");
            compile.root_module.addCMacro("_DARWIN_C_SOURCE", "1");
            compile.root_module.addCMacro("_XOPEN_SOURCE", "700");
        },
        .linux => {
            compile.root_module.addCMacro("__LINUX", "1");
            compile.root_module.addCMacro("_GNU_SOURCE", "1");
        },
        else => {},
    }
    switch (arch) {
        .aarch64 => {
            compile.root_module.addCMacro("__ARM64", "1");
        },
        .x86_64 => {
            compile.root_module.addCMacro("__AMD64", "1");
        },
        .riscv64 => {
            compile.root_module.addCMacro("__RISCV64", "1");
        },
        else => {},
    }
}
