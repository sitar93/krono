Import("env")
import os
import shutil
print("!!! rename_firmware.py: SCRIPT LOADED BY SCONS !!!") # Print globale

def rename_firmware(source, target, env):
    print(">>> rename_firmware.py: rename_firmware CALLED") # Print all'inizio della funzione
    try:
        build_dir = env.subst("$BUILD_DIR")
        version = env["VERSION"]
        prog_name = env.subst("$PROGNAME")
        
        base_name = f"krono_code_{version}"
        print(f">>> rename_firmware.py: build_dir={build_dir}, version={version}, prog_name={prog_name}, base_name={base_name}")
        
        for ext in ['.elf', '.bin', '.hex']:
            src_file = os.path.join(build_dir, f"{prog_name}{ext}")
            dest_file = os.path.join(build_dir, f"{base_name}{ext}")
            print(f">>> rename_firmware.py: Checking for {src_file} to rename to {dest_file}")
            
            if os.path.exists(src_file):
                shutil.move(src_file, dest_file)
                print(f"Renamed: {os.path.basename(dest_file)} (debug print from rename_firmware.py)")
            else:
                print(f">>> rename_firmware.py: Source file {src_file} not found.")
    except Exception as e:
        print(f">>> rename_firmware.py: ERROR in rename_firmware: {e}")
    finally:
        print(">>> rename_firmware.py: rename_firmware END")

env.AddPostAction("buildprog", rename_firmware)
print("!!! rename_firmware.py: AddPostAction for rename_firmware REGISTERED !!!") # Print dopo la registrazione