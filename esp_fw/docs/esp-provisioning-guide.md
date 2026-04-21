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

Enter all of the following:

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

Recommended command:

```bash
cd esp_fw
bash tools/serial_capture.sh /dev/ttyACM0 /tmp/esp_fw-session.log
```

## Expected auth outcomes

- `ok`: device can stream telemetry immediately
- `registration_required`: device is pending approval on RK3588; keep it powered and re-check after approval
- `rejected`: provisioning secret or registration state is wrong; re-provision or reset backend state
