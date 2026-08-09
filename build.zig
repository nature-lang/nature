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
            var file_buffer: [7]u8 = undefined;
            const version = try std.Io.Dir.cwd().readFile(io, "VERSION", &file_buffer);

            break :blk try std.mem.Allocator.dupeSentinel(std.heap.page_allocator, u8, version[0..], 0);
        },
        .BUILD_TIME = blk: {
            const start = std.Io.Clock.real.now(io);
            const elapsed = start.untilNow(io, .cpu_thread);

            const s = try timestampToDate(@as(usize, @intCast(elapsed.toMilliseconds() * -1)), 8);
            break :blk s;
        },
        .BUILD_TYPE = if (optimize == .debug) "debug" else "release",
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

    //build runtime,please use ReleaseFast
    const lib = b.addLibrary(.{
        .name = "libruntime",
        .root_module = b.createModule(.{
            .optimize = optimize,
            .target = target,
            .link_libc = true,
        }),
    });
    setCMacros(lib, target);
    lib.root_module.addIncludePath(.{ .cwd_relative = "./" });
    lib.root_module.addIncludePath(.{ .cwd_relative = "./include/" });
    lib.root_module.addAssemblyFile(.{ .cwd_relative = "./runtime/aco/acosw.S" });

    const runtime_file = try findCFiles(b.allocator, "runtime");
    lib.root_module.addCSourceFiles(.{
        .files = runtime_file.items,
        .flags = &.{},
    });

    lib.root_module.addCSourceFiles(.{
        .files = nature_utils.items,
        .flags = &.{},
    });

    const run_lib_cmd = b.addRunArtifact(lib);
    run_lib_cmd.step.dependOn(b.getInstallStep());

    b.installArtifact(lib);

    const run_lib_step = b.step("lib", "build runtime libary");
    run_lib_step.dependOn(&run_lib_cmd.step);
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

// use LLM AI, but manual review
fn timestampToDate(timestamp: usize, tz_offset: usize) ![:0]u8 {
    var time: usize = timestamp;
    // Handle milliseconds
    if (timestamp > 1000000000000) {
        time = @divFloor(timestamp, 1000);
    }

    // time zone offset
    time += tz_offset * 3600;

    // 1. total days and remaining seconds
    var total_days: usize = @divFloor(time, 86400);
    var remaining_seconds: usize = time % 86400;

    // 2. year
    var year: usize = 1970;
    while (true) {
        const days_this_year: usize = if (isLeap(year)) 366 else 365;
        if (total_days < days_this_year) break;
        total_days -= days_this_year;
        year += 1;
    }

    // 3. month
    var month: usize = 1;
    while (true) {
        const dim = daysInMonth(year, month);
        if (total_days < dim) break;
        total_days -= dim;
        month += 1;
    }

    const day = total_days + 1;

    // 4. hour/minute/second
    const hour: usize = @divFloor(remaining_seconds, 3600);
    remaining_seconds %= 3600;
    const minute: usize = @divFloor(remaining_seconds, 60);
    const second = remaining_seconds % 60;

    const date = try std.mem.Allocator.printSentinel(std.heap.page_allocator, "{}-{}-{} {}:{}:{}", .{
        year,
        month,
        day,
        hour,
        minute,
        second,
    }, 0);

    return date;
}

fn isLeap(year: usize) bool {
    return (year % 4 == 0 and year % 100 != 0) or (year % 400 == 0);
}

fn daysInMonth(year: usize, month: usize) usize {
    const bm: []const u8 = &.{ 1, 3, 5, 7, 8, 10, 12 };

    for (bm) |b| {
        if (b == month) {
            return 31;
        }
    }

    const sm: []const u8 = &.{ 4, 6, 9, 11 };

    for (sm) |s| {
        if (s == month) {
            return 30;
        }
    }

    if (month == 2 and isLeap(year)) {
        return 29;
    } else {
        return 28;
    }
    return 0;
}
