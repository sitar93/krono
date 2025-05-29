Import("env")
print("!!! hex_build.py: SCRIPT LOADED BY SCONS !!!") # Print globale

def post_build_hex(source, target, env):
    print(">>> hex_build.py: post_build_hex CALLED") # Print all'inizio della funzione
    try:
        from os.path import join
        build_dir = env.subst("$BUILD_DIR")
        target_name = env.subst("$PROGNAME")
        
        hex_command = f'arm-none-eabi-objcopy -O ihex "{join(build_dir, target_name)}.elf" "{join(build_dir, target_name)}.hex"'
        print(f">>> hex_build.py: Attempting to execute: {hex_command}")
        
        env.Execute(
            env.VerboseAction(
                hex_command,
                "Generating HEX file (debug print from hex_build.py)"
            )
        )
        print(f">>> hex_build.py: HEX generation command presumably executed for {target_name}.hex")
    except Exception as e:
        print(f">>> hex_build.py: ERROR in post_build_hex: {e}")
    finally:
        print(">>> hex_build.py: post_build_hex END")

env.AddPostAction("buildprog", post_build_hex)
print("!!! hex_build.py: AddPostAction for post_build_hex REGISTERED !!!") # Print dopo la registrazione