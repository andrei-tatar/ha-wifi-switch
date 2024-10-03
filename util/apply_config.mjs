import { readdirSync, readFileSync } from "node:fs";

const CONFIG_DIR = "./util/config";

const configFiles = readdirSync(CONFIG_DIR);

for (const configFile of configFiles) {
  try {
    const config = JSON.parse(
      readFileSync(`${CONFIG_DIR}/${configFile}`).toString()
    );
    let device = configFile.split(".")[0];

    console.log(`\ndevice: ${device}`);

    const host = `${device}.lan`;

    const deviceConfig = await fetch(`http://${host}/api/config`).then((r) =>
      r.json()
    );

    config.mdns = { host: device };
    if (deviceConfig.mqtt) {
      config.mqtt = deviceConfig.mqtt;
    } else {
      //TODO: apply default
      // config.mqtt = {
      //   host: '',
      //   user: '',
      //   password: '',
      // };
    }

    if (deepEqual(config, deviceConfig)) {
      console.log("skip");
      continue;
    }

    console.log("sending config");
    const configBody = JSON.stringify(config);
    const setResponse = await fetch(`http://${host}/api/config`, {
      method: "POST",
      body: configBody,
    });
    if (!setResponse.ok) {
      throw new Error(`could not set config for ${device}`);
    }

    await fetch(`http://${host}/api/reboot`, { method: "POST" });
  } catch (err) {
    console.error(err);
    continue;
  }
}

function deepEqual(x, y) {
  if (x === y) {
    return true;
  }

  if (typeof x == "object" && x != null && typeof y == "object" && y != null) {
    if (Object.keys(x).length != Object.keys(y).length) return false;

    for (var prop in x) {
      if (y.hasOwnProperty(prop)) {
        if (!deepEqual(x[prop], y[prop])) return false;
      } else {
        return false;
      }
    }

    return true;
  }

  return false;
}
