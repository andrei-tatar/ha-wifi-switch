Import("env")
import os
import subprocess


# Returns something like 1.15.0, 1.15.0-1-gab3e64b or 1.15.0-1-gab3e64b-dirty,
# so a device shows which code it runs even when flashed from a local build.
def version_from_git():
  try:
    return subprocess.check_output(
      ["git", "describe", "--tags", "--always", "--dirty"],
      cwd=env.subst("$PROJECT_DIR"),
      stderr=subprocess.DEVNULL,
      text=True,
    ).strip()
  except (OSError, subprocess.CalledProcessError):
    return ""


# VERSION takes priority: release builds use it to set the release's version.
firmware_version = os.environ.get('VERSION', "") or version_from_git() or "0.0.0"

# Strip any leading "v" that might be on the version and
# any leading or trailing periods.
firmware_version = firmware_version.lstrip("v")
firmware_version = firmware_version.strip(".")

print(f'Using version {firmware_version} for the build')

# Append the version to the build defines so it gets baked into the firmware
env.Append(CPPDEFINES=[
  f'BUILD_VERSION=\\"{firmware_version}\\"'
])

# Set the output filename to the name of the environment
env.Replace(PROGNAME=f'ha_switch_{env["PIOENV"]}')
