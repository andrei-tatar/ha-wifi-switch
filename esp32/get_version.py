Import("env")

import os
import zlib

# Open in binary mode (so you don't read two byte line endings on Windows as one byte)
# and use with statement (always do this to avoid leaked file descriptors, unflushed files)
print(f'Reading coprocessor firmware')
with open('../dimmer-coprocessor/.pio/build/tiny_dimmer/firmware.hex', 'rb') as firmware:
  with open('./src/coprocessor.h', 'w') as header:
    data = firmware.read()
    compressed = zlib.compress(data)[2:-4]
    hexdata = compressed.hex()
    firmwareData = [hexdata[i:i+2] for i in range(0, len(hexdata), 2)]

    header.write('#ifndef _COP_H_\n#define _COP_H_\n\n#include <stdint.h>\n\n')
    header.write('const uint8_t COPROCESSOR_FIRMWARE[] = {\n')
    for i in range(0, len(firmwareData), 12):
      line = ', '.join(map(lambda x: '0x'+x, firmwareData[i:i+12]))
      header.write('    '+line+',\n')
    header.write('};\n')
    header.write('\n#endif\n')
    print(f'Compressed from {len(data)} to {len(compressed)}')

# Get the version number from the build environment.
firmware_version = os.environ.get('VERSION', "")

# Clean up the version number
if firmware_version == "":
  # When no version is specified default to "0.0.1" for
  # compatibility with MobiFlight desktop app version checks.
  firmware_version = "0.0.1"

# Strip any leading "v" that might be on the version and
# any leading or trailing periods.
firmware_version = firmware_version.lstrip("v")
firmware_version = firmware_version.strip(".")

print(f'Using version {firmware_version} for the build')

# Append the version to the build defines so it gets baked into the firmware
env.Append(CPPDEFINES=[
  f'BUILD_VERSION=\\"{firmware_version}\\"'
])

# Set the output filename to the name of the board and the version
env.Replace(PROGNAME=f'ha_switch_{env["PIOENV"]}')