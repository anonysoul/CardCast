# Audio Stream Server for M5Cardputer

It sets up an audio stream server on an M5Cardputer, capturing microphone input and streaming it over a Wi-Fi connection to a web page. This allows you to listen to the microphone input from the M5Cardputer remotely through a web browser.

![](./audio-stream.jpg)


## Features

- Streams microphone audio from M5Cardputer to a web page.
- Captures audio at a sample rate of 16000 Hz.
- Provides a simple HTML interface to play the audio stream.
- Configurable via on-screen input for Wi-Fi credentials.
- Sets display brightness to a very low value to improve battery consumption.
- Save Wi-Fi credentials on Non-Volatile-Memory for better experience.


## Installation

- <b>M5Burner</b> : Search into M5CARDPUTER section and simply burn it
- <b>Old school</b> : Build or take the firmware.bin from the github release and flash it


## Usage

- Configure the WiFi : Scan for available Wifi networks and insert password when prompted. 
- Access the Audio Stream : Open a browser and navigate to the local IP adress displayed on the M5cardputer screen.


### DHCP and static IP configuration

After selecting a Wi-Fi network and entering its password (empty for an open
network), use `;` / `.` to move up / down and `Enter` to select a menu item:

- **Connect / Retry connection**: connect using the current settings.
- **Change password**: edit the password, including a previously saved password.
- **Network settings**: choose **DHCP** (default) or **Static IP**.
- **Select another network**: return to the Wi-Fi scan.

For a network without DHCP, choose **Network settings → Static IP** and enter:

| Field | Example |
| --- | --- |
| IP address | `192.168.1.42` |
| Subnet mask | `255.255.255.0` |
| Gateway | `192.168.1.1` |
| Primary DNS | Leave empty, or enter your network's DNS server |
| Secondary DNS | Optional |

Use addresses supplied by your network administrator; the example IP must be
unused and belong to your network. The gateway must be a different usable host
in the same subnet. Standard contiguous masks from /1 through /30 are supported.
DNS is not required to access the audio server by IP, and no public DNS is added
automatically.

Fields are prefilled when editing. Use `Del` to erase and `Enter` to accept each
field, then select **Use settings** to confirm. Invalid parameters show an error
and can be edited again or cancelled. Return to the main menu and select
**Connect**. Open the address displayed on the device to listen to the stream.

Only the most recently connected network's password and network settings are
saved. Selecting that SSID after a restart restores them. Existing saved Wi-Fi
credentials from older firmware use DHCP by default. Failed connections preserve
the saved configuration and keep the current inputs available for editing and
retrying; selecting another network discards unsaved edits.

To restore automatic addressing, select **Network settings → DHCP**, then
**Connect**. To edit settings while streaming, restart the device and enter Wi-Fi
setup again. A successful Wi-Fi connection does not verify gateway reachability
or detect duplicate IP addresses.

### Validation

Build the firmware with `pio run`. Run the host-side IPv4 validation tests with:

```sh
c++ -std=c++11 -Wall -Wextra -pedantic test/network_validation_test.cpp -o /tmp/cardputer-network-test
/tmp/cardputer-network-test
```

Hardware checks: connect to a DHCP-disabled access point using a static address,
play audio from a browser, restart and reuse saved settings, then switch back to
DHCP. Also check incorrect passwords, connection timeouts, retry/edit operations,
and loading settings saved by older firmware. These require a physical device.
