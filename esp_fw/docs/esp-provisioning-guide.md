# ESP Provisioning Guide

## Preconditions

- ESP32-S3 board is powered and flashed with the `esp_fw` firmware
- RK3588 `healthd` service is reachable on the target network
- You know the Wi-Fi credentials and RK3588 TCP address

## Step 1: Join the provisioning AP

When the ESP has no valid config, it enters provisioning mode automatically.

- SSID: `RKHealth-XXXX`
- password: `rkhealth01`
- portal: `http://192.168.4.1/`

The LEDs indicate provisioning mode with the provisioning blink pattern.

## Step 2: Fill the form

The page refreshes the nearby Wi-Fi list automatically every 3 seconds. Pick one SSID from the dropdown, then enter:

- `wifi_ssid`
- `wifi_password`
- `server_ip` (RK3588 host IP)
- `server_port` (normally `19001`)
- `device_id`
- `device_name`
- `device_secret`

## Step 3: Save and wait for reboot

After submission:

- the firmware writes the config to NVS
- the web page returns `saved, restarting`
- the device reboots automatically into STA mode

## Step 4: Verify bring-up

Use serial logs to verify the sequence:

1. Wi-Fi connected
2. TCP target configured
3. auth result is `ok`, `registration_required`, or `rejected`
4. telemetry starts if auth becomes active
5. while waiting in provisioning mode, a heartbeat log appears every 3 seconds with AP client count and the latest scan summary

Recommended command:

```bash
cd esp_fw
bash tools/serial_capture.sh /dev/ttyACM0 /tmp/esp_fw-session.log
```

If your USB adapter enumerates as `/dev/ttyUSB0`, replace the port accordingly. If `idf.py monitor` reports that the path is "not readable", the usual cause is Linux serial-device permissions rather than firmware logic: check `ls -l /dev/ttyUSB0`, confirm your user is in the `dialout` group, then re-login.

## Expected auth outcomes

- `ok`: device can stream telemetry immediately
- `registration_required`: device is pending approval on RK3588; keep it powered and re-check after approval
- `rejected`: provisioning secret or registration state is wrong; re-provision or reset backend state
