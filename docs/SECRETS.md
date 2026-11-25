# Secrets / credentials

**Nothing secret is stored in this repository.** WiFi and OTA passwords are
provided locally and are gitignored.

## OTA password

`main/my_machine.h` includes `main/ota_secret.h` when it exists. That file is
gitignored. Create it with:

```sh
python3 scripts/make_ota_secret.py 'your-strong-password'
# or let it generate one:
python3 scripts/make_ota_secret.py
```

Then rebuild and deploy:

```sh
pio run -e dlc32-max
curl -H "X-OTA-Password: <old password>" --data-binary @.pio/build/dlc32-max/dlc32-max.bin http://<ip>:8080/ota
```

If `OTA_PASSWORD` is empty the OTA endpoint has **no authentication** (only
reachable on your LAN) — always set it on a shared network.

## WiFi

WiFi credentials are not compiled in: set them on the controller with `$74`
(SSID) / `$75` (password), or `tools/set_wifi.py --ssid ... --password ...`.
They live in the controller's NVS, not in this repo.

## If a secret leaks

Change it (rotate): regenerate `ota_secret.h`, rebuild, deploy. Rotating makes
any leaked copy useless regardless of git history. Removing it from history is
only cosmetic.
