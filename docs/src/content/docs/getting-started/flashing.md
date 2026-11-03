---
title: Firmware installieren
description: Clean Image auf einem GeekMagic SmallTV oder SmallTV Ultra mit ESP8266 installieren und aktualisieren.
---

Diese Anleitung gilt ausschließlich für **SmallTV und SmallTV Ultra mit ESP8266**. Verwende sie nicht für ein Gerät mit ESP32 oder ESP32-C2. Das Clean Image ersetzt die installierte Firmware; sichere die Original-Firmware vorher, falls du zurückwechseln möchtest.

## Image beziehen

Ein fertiges Image wird als GitHub-Actions-Artefakt oder als Release-Datei `smalltv-clean-firmware.bin` bereitgestellt, sobald ein entsprechender Build veröffentlicht wurde. Bis dahin kannst du es lokal bauen (siehe [Image bauen](/smalltv-mod/reference/building/)). Die PlatformIO-Ausgabedatei heißt `firmware.bin`.

## SmallTV mit ESP8266

Wenn die vorhandene Firmware einen manuellen Update-Upload anbietet:

1. Ermittle die IP-Adresse des SmallTV.
2. Öffne `http://<IP-Adresse>/update` oder den Firmware-Upload in der Systemseite.
3. Wähle das Clean Image aus und starte den Upload.
4. Unterbrich die Stromversorgung während des Schreibens nicht. Das Gerät startet danach neu.

Falls die Update-Seite unter `/update` nicht erreichbar ist, verwende den Upload in der Weboberfläche oder den seriellen UART-Anschluss. Ein UART-Flash benötigt einen 3,3-V-USB-UART-Adapter und den korrekten Flash-Modus.

## SmallTV Ultra

SmallTV Ultra nutzt ebenfalls einen ESP8266, aber die originale Ultra-Firmware kann einen großen Flash-Bereich für eigene Medien reservieren. Der dortige Update-Dialog kann ein vollständiges Image deshalb mit **Not Enough Space** ablehnen.

Wenn das passiert, ist für die Erstinstallation der SmallTV-Loader nötig: Lade den Loader über den ursprünglichen Update-Dialog, verbinde dich danach mit dessen WLAN-Zugangspunkt und lade das Clean Image über die Loader-Seite hoch. Folge dabei der Anleitung und den Dateien des Loaders, den du bereits hast. Der Loader wird für spätere Updates nicht erneut benötigt, sobald das Gerät mit dem Clean Image läuft.

## Nach dem Flashen

Beim ersten Start stellt die Firmware den Hotspot `SmallTV-Setup` bereit. Verbinde dich damit und öffne bei Bedarf `http://192.168.4.1`, um dein 2,4-GHz-WLAN einzurichten. Danach erreichst du die Weboberfläche unter der vom Display angezeigten IP-Adresse.

Spätere Aktualisierungen erfolgen über den manuellen Firmware-Upload im Bereich **System**. Prüfe immer, dass die Datei für ESP8266 und Clean Image bestimmt ist. Nach einem Firmwarewechsel bleiben Einstellungen normalerweise erhalten; vor größeren Änderungen empfiehlt sich ein Export im Systembereich.
