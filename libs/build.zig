const std = @import("std");

const lib_prefix = "libs/";

const uv_sources = [_][]const u8{"uv.c"};
const win_uv_sources = uv_sources ++ [_][]const u8{};

pub fn addUv(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, hl_lib: *std.Build.Step.Compile, comptime c_flags: []const []const u8) *std.Build.Step.Compile {
    const uv_lib = b.addSharedLibrary(.{
        .name = "uv",
        .target = target,
        .optimize = optimize,
    });

    uv_lib.linkLibrary(hl_lib);
    uv_lib.addIncludePath(.{ .path = "include/" });
    uv_lib.addIncludePath(.{ .path = "src/" });
    uv_lib.addCSourceFiles(.{
        .root = .{ .path = lib_prefix ++ "uv" },
        .files = &uv_sources,
        .flags = c_flags,
    });

    if (target.result.os.tag == .windows) {} else {
        uv_lib.linkSystemLibrary("uv");
    }

    return uv_lib;
}
