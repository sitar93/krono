Import("env")
import os
import shutil

print("!!! rename_firmware.py: SCRIPT LOADED BY SCONS !!!")


def _has_dfu_suffix(path):
    """True if file ends with a 16-byte dfu-suffix (signature UFD, bLength 16)."""
    try:
        size = os.path.getsize(path)
        if size < 16:
            return False
        with open(path, "rb") as f:
            f.seek(-16, os.SEEK_END)
            tail = f.read(16)
        return tail[8:11] == b"UFD" and tail[11] == 16
    except OSError:
        return False


def rename_firmware(source, target, env):
    """
    Historical Jolin release layout (required by https://jolin.tech/krono_updater):

    - krono_code_<ver>.bin  = PlatformIO `firmware.bin` **with DFU suffix** (rename only)
    - krono_code_<ver>.hex  = Intel HEX from ELF
    - krono_code_<ver>.elf  = ELF

    Do NOT strip the DFU suffix or 0xFF-pad the primary .bin — that broke online flashing.
    Optional Cube-only raw image: krono_code_<ver>_raw.bin (objcopy, no suffix).
    """
    print(">>> rename_firmware.py: rename_firmware CALLED")
    try:
        build_dir = env.subst("$BUILD_DIR")
        version = env["VERSION"]
        prog_name = env.subst("$PROGNAME")
        base_name = f"krono_code_{version}"
        print(
            f">>> rename_firmware.py: build_dir={build_dir}, version={version}, "
            f"prog_name={prog_name}, base_name={base_name}"
        )

        elf_src = os.path.join(build_dir, f"{prog_name}.elf")
        hex_src = os.path.join(build_dir, f"{prog_name}.hex")
        bin_src = os.path.join(build_dir, f"{prog_name}.bin")

        elf_dst = os.path.join(build_dir, f"{base_name}.elf")
        hex_dst = os.path.join(build_dir, f"{base_name}.hex")
        bin_dst = os.path.join(build_dir, f"{base_name}.bin")
        raw_dst = os.path.join(build_dir, f"{base_name}_raw.bin")

        if os.path.exists(elf_src):
            shutil.move(elf_src, elf_dst)
            print(f"Renamed: {os.path.basename(elf_dst)}")
        elif not os.path.exists(elf_dst):
            print(f">>> rename_firmware.py: ERROR missing ELF ({elf_src})")
            return

        if os.path.exists(hex_src):
            shutil.move(hex_src, hex_dst)
            print(f"Renamed: {os.path.basename(hex_dst)}")

        # Primary .bin for jolin.tech = PlatformIO output including DFU suffix (as v1.4.0).
        if os.path.exists(bin_src):
            if not _has_dfu_suffix(bin_src):
                print(
                    ">>> rename_firmware.py: WARNING firmware.bin has no DFU suffix "
                    "(upload_protocol=dfu normally adds it)"
                )
            shutil.move(bin_src, bin_dst)
            print(
                f"Renamed: {os.path.basename(bin_dst)} "
                f"({os.path.getsize(bin_dst)} bytes, DFU-suffixed for online updater)"
            )
        else:
            print(f">>> rename_firmware.py: Source bin {bin_src} not found.")

        # Optional raw image for CubeProgrammer / tools that hate the suffix.
        objcopy = env.subst("$OBJCOPY")
        if not objcopy:
            objcopy = "arm-none-eabi-objcopy"
        if os.path.exists(elf_dst):
            raw_cmd = f'"{objcopy}" -O binary "{elf_dst}" "{raw_dst}"'
            env.Execute(env.VerboseAction(raw_cmd, "Generating optional RAW bin (Cube)"))
            if os.path.exists(raw_dst):
                print(f"Optional: {os.path.basename(raw_dst)} ({os.path.getsize(raw_dst)} bytes)")
    except Exception as e:
        print(f">>> rename_firmware.py: ERROR in rename_firmware: {e}")
    finally:
        print(">>> rename_firmware.py: rename_firmware END")


env.AddPostAction("buildprog", rename_firmware)
print("!!! rename_firmware.py: AddPostAction for rename_firmware REGISTERED !!!")
