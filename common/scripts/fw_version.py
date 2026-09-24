import re
import subprocess

Import("env")


def _describe(cwd):
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=cwd,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"

    if result.returncode == 0:
        out = result.stdout.strip()
        if out:
            return out
    return "unknown"


def _sanitize(value):
    return re.sub(r"[^A-Za-z0-9._+-]", "", value) or "unknown"


ver = _sanitize(_describe(env["PROJECT_DIR"]))
env["HEATER_FW_VERSION"] = ver
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(ver))])
print("FW_VERSION: %s" % ver)
