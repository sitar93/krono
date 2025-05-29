Import("env")
import re

def get_version():
    """Extracts version from the last non-empty line in changelog.txt, preserving the 'v' prefix"""
    try:
        with open("CHANGELOG.txt", "r") as f:
            # Get last non-empty line
            lines = [line.strip() for line in f.readlines() if line.strip()]
            last_line = lines[-1] if lines else ""
            
            # Extract first word (version)
            version = last_line.split()[0] if last_line else "v0.0.0"
            
            # Ensure version starts with 'v' and has correct format
            if not version.startswith('v'):
                version = 'v' + version
                
            if not re.match(r'^v\d+\.\d+\.\d+$', version):
                print(f"Warning: Version '{version}' doesn't match expected format 'vX.X.X'")
                version = "v0.0.0"
                
            return version
            
    except Exception as e:
        print(f"Error reading changelog.txt: {e}")
        return "v0.0.0"  # Fallback version

# Store version in build environment
env.Append(
    VERSION=get_version(),
    CPPDEFINES=[("FW_VERSION", f'\\"{get_version()}\\"')]  # Optional: for code access
)