import sys

def log(*args, **kwargs):
    print(*args, **kwargs)
    sys.stdout.flush()

log("======================================================")
log("  KRONO Module Build")
log("  Simplified structure for PlatformIO compatibility")
log("=======================================================")