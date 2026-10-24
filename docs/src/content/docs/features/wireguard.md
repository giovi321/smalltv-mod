---
title: WireGuard VPN
description: An optional built-in WireGuard tunnel, so you can reach the device from outside your network without opening its plain-HTTP port to the internet.
---

The settings page speaks plain HTTP, and its password is optional and off by default. Either way it is fine on your own network and a bad idea on the open internet, so do not port-forward it. The WireGuard client is the alternative: the device joins your VPN itself, and you reach it on its tunnel address from anywhere the VPN reaches.

## Which devices have it

Every ESP32 board: the **ESP32-C2**, the **SmallTV Pro**, and the **NM-TV-154**. The ESP8266 boards cannot have it. What decides this is flash, not what the chip could do.

| Board | WireGuard | Why |
|---|---|---|
| SmallTV (ESP8266) and SmallTV-ultra | no | The chip has ~80 KB of heap in total and the ticker's TLS already runs close to it. WireGuard on this platform also pulls in a second crypto stack alongside the BearSSL already in the image. |
| SmallTV (ESP32-C2) | yes, in the only image | The image fits its 1,572,864-byte update slot with the client in it. |
| NM-TV-154 (classic ESP32) | yes, in a second image | The classic ESP32's framework libraries cost about 100 KB more than the C2's, so this is the tightest fit of the ESP32 boards: 1,434,192 bytes of the same 1,572,864-byte slot without the client, 1,479,984 bytes (94.1%) with it on v3.0.0. It fits with 92,880 bytes to spare, but the tunnel takes about a third of the room the plain image has left, and this slot can never be enlarged, so it ships as the separate `-esp32-wg` image rather than in the default one. |
| SmallTV Pro (classic ESP32, 8 MB) | yes, in the only image | Same chip family as the NM-TV-154, but its stock partition layout gives each OTA slot 2,228,224 bytes instead of 1,572,864, so the client fits with hundreds of kilobytes left over. |

So on the NM-TV-154 you choose at install time, and you can change your mind later. `smalltv-mod-firmware-esp32.bin` has no tunnel, `smalltv-mod-firmware-esp32-wg.bin` has one, and moving between them is a manual firmware upload in the System tab, over the air, with your settings intact. A self-update never crosses between the two: each build fetches the release asset that matches its own variant. [Which release file to download](/smalltv-mod/reference/release-assets/#switch-a-device-from-one-variant-to-the-other) walks through the swap.

A device without the client still shows the section as absent rather than broken: the settings page simply has no WireGuard card.

## What you need

A WireGuard peer you can add this device to: a router that speaks WireGuard, a VPN server you run, or a commercial provider that gives you a full config. You need five things from it.

- Your own key pair. Generate it on your computer with `wg genkey | tee device.key | wg pubkey > device.pub`. The private half goes into the device, the public half goes to the server as this device's peer. Many router and provider interfaces generate the pair for you and hand you both halves, which works just as well.
- The server's public key.
- The server's hostname or IP, and its UDP port (51820 unless told otherwise).
- The address the device should have inside the tunnel, as a CIDR, for example `10.6.0.12/32`.
- Which addresses the device should route through the tunnel, for example `10.6.0.0/24`.

There is one more field, Keepalive, which you do not need anything from the server for. It is how often the device sends an empty authenticated packet to hold a NAT or firewall mapping open, in seconds. The default of 25 is the usual value and works behind almost every home router; 0 turns it off, which is only worth doing when the device sits on a public address with nothing stateful in between.

The device does not generate keys and cannot show you its own public key, so keep the public half from the step above; it is what the server needs.

## Setting it up

1. Add the device as a peer on your WireGuard server, with the public key you generated and the tunnel address you picked for it.
2. Open the settings page, go to the **WiFi** tab, and find the **WireGuard VPN** card.
3. Paste the private key, the server's public key, the endpoint host and port, the tunnel address, and the allowed IPs. Leave Keepalive at 25.
4. Tick **Enable the tunnel** and press **Save settings**.
5. Watch the line at the top of the card. Within a few seconds it should read "Tunnel up".

Once it is up, the settings page answers on the tunnel address as well as on the LAN one: `http://10.6.0.12` in the example above.

The private key is stored on the device and included in a settings export, so treat an exported config file like a password. The settings page never reads the key back; the field shows "(unchanged)" and leaving it blank keeps the stored one, so you can edit the rest of the form without handling the secret again.

## Allowed IPs, and what the device can actually reach

This is the setting people get wrong, and the symptom is a tunnel that handshakes but cannot be pinged.

The device's network stack has no routing table. What decides which addresses are reachable through the tunnel is the interface's netmask, and a `/32` address matches nothing but itself. So the firmware widens it for you: the tunnel interface takes the widest allowed-IPs range that contains its own address. A device at `10.6.0.12/32` with allowed IPs `10.6.0.0/24` comes up as `10.6.0.12/24` and can talk to everything on `10.6.0.0/24`.

So list the subnet the other end is on, and not only the device's own address. If you set allowed IPs to `0.0.0.0/0`, the tunnel becomes the device's default route and everything, including the ticker's price fetches, goes through the VPN.

## Reading the status line

The card's top line says what the tunnel is doing, and names the likely cause when it is not working.

| Line | What it means |
|---|---|
| Tunnel off | The tunnel is not enabled. |
| Tunnel suspended after repeated reboots | The crash guard below has held it down. |
| Tunnel not started: a field is empty or malformed | Something in the form did not parse. The usual cause is a tunnel address with no prefix, or a typo in one. |
| Waiting for the endpoint name to resolve | DNS has not answered yet. Check the hostname, and that the device has working DNS. |
| Handshakes sent to `<ip>` with no reply | The device found the server and is talking to it, and the server is not answering. Check that the peer on the server carries this device's public key, and that UDP to that port reaches it. |
| Tunnel up, last handshake N ago | Working. |

The Status tab also carries a one-word VPN line, so you can see the tunnel state without opening the WiFi tab.

## The crash guard

A tunnel bug must never lock you out of the device's own settings page. The firmware counts consecutive crash reboots that happen while the tunnel is enabled, and after three in a row it holds the tunnel down at the next boot and boots normally without it.

Three things clear the count: any clean boot, the first completed handshake, and saving the WireGuard settings again. That last one is deliberate; re-saving is you saying the configuration is fixed and the device should try again.

## How it behaves while running

- It waits for the clock before the first handshake, up to 90 seconds. WireGuard stamps handshakes with the wall clock and a peer rejects one stamped wrong, so enabling the tunnel also starts NTP whether or not night mode is on.
- A failed bring-up backs off from 5 seconds to 60 seconds and keeps trying.
- Three minutes without a handshake tears the tunnel down and rebuilds it, which resolves the endpoint hostname again. That is what makes a peer on dynamic DNS come back after its address changes.
- Bring-up runs from the main loop in small steps, so the settings page stays responsive while the tunnel is connecting.

## Limits

- One tunnel and one peer.
- IPv4 allowed IPs only.
- No preshared key.
- The device cannot show its own public key; keep the one you generated.
- Up to four allowed-IPs entries, plus the device's own address. That is a compile-time limit.
- The endpoint hostname is resolved when the tunnel is built, not per packet.

## Where the client comes from

The tunnel itself is [droscy/esp_wireguard](https://github.com/droscy/esp_wireguard) 0.4.5, pinned exactly, the same component ESPHome ships. The firmware adds the configuration, the state machine, the crash guard, and the web UI around it.
