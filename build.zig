const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{});

    // const os = target.result.os.tag;
    // const arch = target.result.cpu.arch;
    const exe = b.addExecutable(.{
        .name = "nature",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
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

    exe.addIncludePath(.{ .cwd_relative = "./" });
    exe.addIncludePath(.{ .cwd_relative = "./include/" });

    setCMacros(exe, target);

    const nature_utils = try findCFiles(b.allocator, "utils");
    defer {
        for (nature_utils.items) |path| b.allocator.free(path);
    }
    exe.addCSourceFiles(.{ .files = nature_utils.items, .flags = &.{"-std=gnu11"} });

    const nature_main = &.{"main.c"};
    const nature_src = try findCFiles(b.allocator, "src");
    defer {
        for (nature_src.items) |path| b.allocator.free(path);
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

pub fn findCFiles(allocator: std.mem.Allocator, dir_path: []const u8) !std.ArrayList([]const u8) {
    var result = std.ArrayList([]const u8){};
    errdefer result.deinit(allocator);

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
                try result.append(allocator, file_path);
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
