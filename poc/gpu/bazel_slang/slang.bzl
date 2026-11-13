"""slang_spirv: compile one .slang entry point to SPIR-V with a pinned slangc run in the exec configuration."""

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out)
    args = ctx.actions.args()
    args.add(ctx.file.src)
    args.add_all(["-target", "spirv", "-entry", ctx.attr.entry, "-stage", "compute"])
    args.add_all(ctx.attr.defines, before_each = "-D")
    args.add("-o", out)
    ctx.actions.run(
        executable = ctx.executable._slangc,
        inputs = depset([ctx.file.src] + ctx.files.deps),
        tools = [ctx.attr._slangc_files[DefaultInfo].files],
        outputs = [out],
        arguments = [args],
        mnemonic = "SlangSpirv",
        progress_message = "slangc %s -> SPIR-V" % ctx.file.src.short_path,
        # no env: slangc must not need $HOME, a cache dir or the host PATH
        use_default_shell_env = False,
    )
    return DefaultInfo(files = depset([out]))

slang_spirv = rule(
    implementation = _impl,
    attrs = {
        "src": attr.label(allow_single_file = [".slang"], mandatory = True),
        "deps": attr.label_list(allow_files = [".slang"]),
        "entry": attr.string(mandatory = True),
        "out": attr.string(mandatory = True),
        "defines": attr.string_list(),
        "_slangc": attr.label(default = "//:slangc", executable = True, cfg = "exec"),
        "_slangc_files": attr.label(default = "//:slangc_files", cfg = "exec"),
    },
)
