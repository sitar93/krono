Import("env")
import re

def get_version():
    """Extracts version from the last non-empty line in CHANGELOG.txt, preserving the 'v' prefix.

    Accepts vX.Y.Z or vX.Y.Z.W (patch train under a release, e.g. v1.4.1.3).
    """
    try:
        with open("CHANGELOG.txt", "r") as f:
            lines = [line.strip() for line in f.readlines() if line.strip()]
            last_line = lines[-1] if lines else ""

            version = last_line.split()[0] if last_line else "v0.0.0"

            if not version.startswith('v'):
                version = 'v' + version

            if not re.match(r'^v\d+\.\d+\.\d+(\.\d+)?$', version):
                print(f"Warning: Version '{version}' doesn't match expected format 'vX.Y.Z' or 'vX.Y.Z.W'")
                version = "v0.0.0"

            return version

    except Exception as e:
        print(f"Error reading changelog.txt: {e}")
        return "v0.0.0"

env.Append(
    VERSION=get_version(),
    CPPDEFINES=[("FW_VERSION", f'\\"{get_version()}\\"')]
)
