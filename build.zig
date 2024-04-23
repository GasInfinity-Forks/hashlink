const std = @import("std");
const libs = @import("libs/build.zig");

const hashlink_major = 1;
const hashlink_minor = 14;
const hashlink_patch = 0;
const hashlink_version = std.SemanticVersion{ .major = hashlink_major, .minor = hashlink_minor, .patch = hashlink_patch };
const hashlink_version_string = std.fmt.comptimePrint("{}", .{hashlink_version});

const pcre_srcs = [_][]const u8{
    "include/pcre/pcre2_auto_possess.c",
    "include/pcre/pcre2_chartables.c",
    "include/pcre/pcre2_compile.c",
    "include/pcre/pcre2_config.c",
    "include/pcre/pcre2_context.c",
    "include/pcre/pcre2_convert.c",
    "include/pcre/pcre2_dfa_match.c",
    "include/pcre/pcre2_error.c",
    "include/pcre/pcre2_extuni.c",
    "include/pcre/pcre2_find_bracket.c",
    "include/pcre/pcre2_jit_compile.c",
    "include/pcre/pcre2_maketables.c",
    "include/pcre/pcre2_match_data.c",
    "include/pcre/pcre2_match.c",
    "include/pcre/pcre2_newline.c",
    "include/pcre/pcre2_ord2utf.c",
    "include/pcre/pcre2_pattern_info.c",
    "include/pcre/pcre2_script_run.c",
    "include/pcre/pcre2_serialize.c",
    "include/pcre/pcre2_string_utils.c",
    "include/pcre/pcre2_study.c",
    "include/pcre/pcre2_substitute.c",
    "include/pcre/pcre2_substring.c",
    "include/pcre/pcre2_tables.c",
    "include/pcre/pcre2_ucd.c",
    "include/pcre/pcre2_valid_utf.c",
    "include/pcre/pcre2_xclass.c",
};

const hx_std_srcs = [_][]const u8{
    "src/std/array.c",  "src/std/buffer.c", "src/std/bytes.c", "src/std/cast.c",  "src/std/date.c", "src/std/error.c",  "src/std/debug.c",
    "src/std/file.c",   "src/std/fun.c",    "src/std/maps.c",  "src/std/math.c",  "src/std/obj.c",  "src/std/random.c", "src/std/regexp.c",
    "src/std/socket.c", "src/std/string.c", "src/std/sys.c",   "src/std/types.c", "src/std/ucs2.c", "src/std/thread.c", "src/std/process.c",
    "src/std/track.c",
};

const hx_runtime_srcs = [_][]const u8{"src/gc.c"};

const lhl_srcs = pcre_srcs ++ hx_std_srcs ++ hx_runtime_srcs;
const lhl_darwin_srcs = lhl_srcs ++ [_][]const u8{ "include/mdbg/mdbg.c", "include/mdbg/mach_excServer.c", "include/mdbg/mach_excUser.c" };
const lhl_ios_srcs = lhl_darwin_srcs ++ [_][]const u8{"src/std/sys_ios.m"};
const lhl_android_srcs = lhl_srcs ++ [_][]const u8{"src/std/sys_android.c"};

const hl_srcs = [_][]const u8{ "src/main.c", "src/jit.c", "src/interp.c", "src/code.c", "src/module.c", "src/debugger.c", "src/profile.c" };

const global_c_flags = [_][]const u8{
    "-std=c11",
};

fn addGlobalIncludes(b: *std.Build, c: *std.Build.Step.Compile) void {
    c.addIncludePath(b.path("src/"));
    c.addIncludePath(b.path("include/"));
    c.addIncludePath(b.path("include/pcre/"));
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const build_libs = b.option(bool, "build_libs", "Build all the bundled libs with hashlink") orelse false;

    if (!target.query.isNative()) {
        std.debug.print("Cannot cross-compile hashlink yet!\n", .{});
        return;
    }

    const hl_lib = b.addSharedLibrary(.{
        .name = "hl",
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .version = hashlink_version,
    });

    if (target.result.os.tag == .windows) {
        hl_lib.linkSystemLibrary("ws2_32");
        hl_lib.linkSystemLibrary("user32");
    }

    addGlobalIncludes(b, hl_lib);
    hl_lib.addCSourceFiles(.{
        .files = &(switch (target.result.os.tag) {
            .ios => lhl_ios_srcs,
            .linux => if (target.result.isAndroid()) lhl_android_srcs else lhl_srcs,
            else => if (target.result.isDarwin()) lhl_darwin_srcs else lhl_srcs,
        }),
        .flags = &(global_c_flags ++ [_][]const u8{ "-DLIBHL_EXPORTS", "-DPCRE2_CODE_UNIT_WIDTH=16", "-DHAVE_CONFIG_H" }),
    });

    const hl_exe = b.addExecutable(.{
        .name = "hl",
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .version = hashlink_version,
    });

    hl_exe.root_module.addRPathSpecial("$ORIGIN"); // TODO: Set rpath depending on option
    hl_exe.linkLibrary(hl_lib);
    addGlobalIncludes(b, hl_exe);
    hl_exe.addCSourceFiles(.{
        .files = &hl_srcs,
        .flags = &global_c_flags,
    });

    b.installArtifact(hl_lib);
    b.installArtifact(hl_exe);

    if (build_libs) {
        const uv = libs.addUv(b, target, optimize, hl_lib, &global_c_flags);
        b.getInstallStep().dependOn(&b.addInstallBinFile(uv.getEmittedBin(), "uv.hdll").step);
    }

    addHaxeTests(b, target, hl_exe, hl_lib);
}

const HaxeTest = struct {
    class_path: []const u8,
    main: []const u8,
    out_path: []const u8,
};

const haxe_hlb_tests = [_]HaxeTest{
    .{ .class_path = "other/tests/", .main = "HelloWorld", .out_path = "hello-test.hl" },
    .{ .class_path = "other/tests/", .main = "Threads", .out_path = "threads-test.hl" },
};

const haxe_hlc_tests = [_]HaxeTest{
    .{ .class_path = "other/tests/", .main = "HelloWorld", .out_path = "hello-testc/hello-test.c" },
    .{ .class_path = "other/tests/", .main = "Threads", .out_path = "threads-testc/threads-test.c" },
};

fn addHaxeTests(b: *std.Build, target: std.Build.ResolvedTarget, hl_exe: *std.Build.Step.Compile, hl_lib: *std.Build.Step.Compile) void {
    const haxe_compiler = b.findProgram(&[_][]const u8{"haxe"}, &[_][]const u8{}) catch {
        std.debug.print("Could not find a haxe compiler, tests won't be available\n", .{});
        return;
    };

    if (!target.query.isNative()) {
        std.debug.print("Cannot add tests while cross compiling, tests won't be available\n", .{});
        return;
    }

    const test_bytecode = b.step("testb", "Test Hashlink locally with the included haxe tests");

    inline for (haxe_hlb_tests) |hlb_test| {
        const compile_bytecode = b.addSystemCommand(&[_][]const u8{haxe_compiler});

        compile_bytecode.addArg("-cp");
        compile_bytecode.addPrefixedDirectorySourceArg(b.build_root.path.?, b.path(hlb_test.class_path));
        compile_bytecode.addArg("-hl");
        const hl_bytecode_file = compile_bytecode.addOutputFileArg(hlb_test.out_path);
        compile_bytecode.addArgs(&[_][]const u8{ "-main", hlb_test.main });

        const run_bytecode = b.addRunArtifact(hl_exe);
        run_bytecode.addFileArg(hl_bytecode_file);
        _ = run_bytecode.captureStdOut();

        test_bytecode.dependOn(&run_bytecode.step);
    }

    const test_compilation = b.step("testc", "Test Hashlink/C locally with the included haxe tests");

    inline for (haxe_hlc_tests) |hlc_test| {
        const compile_to_hlc = b.addSystemCommand(&[_][]const u8{haxe_compiler});

        compile_to_hlc.addArg("-cp");
        compile_to_hlc.addPrefixedDirectorySourceArg(b.build_root.path.?, b.path(hlc_test.class_path));
        compile_to_hlc.addArg("-hl");
        const hl_c_file = compile_to_hlc.addOutputFileArg(hlc_test.out_path);
        compile_to_hlc.addArgs(&[_][]const u8{ "-main", hlc_test.main });

        const hlc_exe = b.addExecutable(.{
            .name = hlc_test.main,
            .link_libc = true,
            .target = target,
            .optimize = .Debug,
        });

        hlc_exe.linkLibrary(hl_lib);
        hlc_exe.addIncludePath(hl_c_file.dirname());
        addGlobalIncludes(b, hlc_exe);
        hlc_exe.addCSourceFile(.{ .file = hl_c_file, .flags = &global_c_flags });

        const run_exe = b.addRunArtifact(hlc_exe);
        _ = run_exe.captureStdOut();

        test_compilation.dependOn(&run_exe.step);
    }

    const test_version = b.addRunArtifact(hl_exe);
    test_version.addArg("--version");
    test_version.expectStdOutEqual(hashlink_version_string);

    const test_all = b.step("test", "Test everything");
    test_all.dependOn(test_bytecode);
    test_all.dependOn(test_compilation);
    test_all.dependOn(&test_version.step);
}
