Import("env")
import os
import shutil

def rename_firmware(source, target, env):
    """Renames output files to krono_code_vX.X.X format"""
    build_dir = env.subst("$BUILD_DIR")
    version = env["VERSION"]  # Uses pre-processed version
    
    # Base filename with version
    base_name = f"krono_code_{version}"
    
    # Rename all output formats
    for ext in ['.elf', '.bin', '.hex']:
        src = os.path.join(build_dir, f"firmware{ext}")
        dest = os.path.join(build_dir, f"{base_name}{ext}")
        
        if os.path.exists(src):
            shutil.move(src, dest)
            print(f"Renamed: {os.path.basename(dest)}")

# Execute after successful build
env.AddPostAction("buildprog", rename_firmware)