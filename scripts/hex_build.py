Import("env")
import os
import glob
print("!!! hex_build.py: SCRIPT LOADED BY SCONS !!!") # Print globale

def post_build_hex(source, target, env):
    print(">>> hex_build.py: post_build_hex CALLED") # Print all'inizio della funzione
    try:
        from os.path import join
        build_dir = env.subst("$BUILD_DIR")
        target_name = env.subst("$PROGNAME")
        current_version = env.get("VERSION", "v0.0.0")
        
        # Clean up old firmware files before building new ones
        print(f">>> hex_build.py: Cleaning old firmware files (current version: {current_version})")
        for ext in ['elf', 'bin', 'hex']:
            pattern = join(build_dir, f"krono_code_v*.{ext}")
            old_files = glob.glob(pattern)
            for old_file in old_files:
                if current_version not in os.path.basename(old_file):
                    try:
                        os.remove(old_file)
                        print(f">>> hex_build.py: Removed old file: {os.path.basename(old_file)}")
                    except Exception as e:
                        print(f">>> hex_build.py: Failed to remove {old_file}: {e}")
        
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