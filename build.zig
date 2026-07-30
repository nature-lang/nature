const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{});

    var threaded = std.Io.Threaded.init(std.heap.page_allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

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
            const file = try std.Io.Dir.cwd().openFile(io, "./VERSION", .{});
            defer file.close(io);
            var file_buffer: [7]u8 = undefined;
            const n = try file.readPositionalAll(io, &file_buffer, 0);

            break :blk file_buffer[0..n];
        },
        // TODO zig std time already update,build_time need afresh realize.
        .BUILD_TIME = 0,
        .BUILD_TYPE = if (optimize == .Debug) "debug" else "release",
    });

    exe.root_module.addConfigHeader(config_header);
    exe.root_module.addIncludePath(.{ .cwd_relative = "./" });
    exe.root_module.addIncludePath(.{ .cwd_relative = "./include/" });

    setCMacros(exe, target);

    const nature_utils = try findCFiles(b.allocator, "utils");
    defer {
        for (nature_utils.items) |path| b.allocator.free(path);
    }
    exe.root_module.addCSourceFiles(.{ .files = nature_utils.items, .flags = &.{"-std=gnu11"} });

    const nature_main = &.{"main.c"};
    const nature_src = try findCFiles(b.allocator, "src");
    defer {
        for (nature_src.items) |path| b.allocator.free(path);
    }

    exe.root_module.addCSourceFiles(.{ .files = nature_main, .flags = &.{"-std=gnu11"} });
    exe.root_module.addCSourceFiles(.{ .files = nature_src.items, .flags = &.{} });

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}

pub fn findCFiles(allocator: std.mem.Allocator, dir_path: []const u8) !std.ArrayList([]const u8) {
    var result = std.ArrayList([]const u8).empty;
    errdefer result.deinit(allocator);

    try recursiveFindCFiles(allocator, dir_path, &result);
    return result;
}

fn recursiveFindCFiles(
    allocator: std.mem.Allocator,
    base_dir: []const u8,
    result: *std.ArrayList([]const u8),
) !void {
    var threaded = std.Io.Threaded.init(std.heap.page_allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var dir = std.Io.Dir.cwd().openDir(io, base_dir, .{ .iterate = true }) catch |err| {
        std.log.debug(" Failed to open directory '{s}': {s}", .{ base_dir, @errorName(err) });
        return;
    };
    defer dir.close(io);

    var iter = dir.iterate();
    while (try iter.next(io)) |entry| {
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
        .windows => {
            compile.root_module.addCMacro("__WINDOWS", "1");
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
