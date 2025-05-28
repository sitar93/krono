Import("env")

def post_build_hex(source, target, env):
    from os.path import join
    build_dir = env.subst("$BUILD_DIR")
    target_name = env.subst("$PROGNAME")
    
    # Comando per generare HEX
    env.Execute(
        env.VerboseAction(
            f'arm-none-eabi-objcopy -O ihex "{join(build_dir, target_name)}.elf" "{join(build_dir, target_name)}.hex"',
            "Generating HEX file"
        )
    )

env.AddPostAction("buildprog", post_build_hex)