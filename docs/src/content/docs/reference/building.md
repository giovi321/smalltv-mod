---
title: Image bauen
description: Clean Image für ESP8266 mit PlatformIO kompilieren und das Ergebnis finden.
---

Das Clean Image wird mit PlatformIO für den ESP8266 gebaut. Der Quellcode stammt aus dem SmallTV-Mod-Projekt; das Ziel `smalltv_clean` aktiviert die schlanke Theme-Variante und schließt die hier nicht benötigten Anzeigemodi aus.

Gepflegt wird dieser Fork von **Jeff-Amdg**. Die GitHub-Actions-Workflows bauen und veröffentlichen die Dokumentation und Firmware für den Branch `clean-image`.

## Voraussetzungen

- PlatformIO Core (Befehl `pio`) oder PlatformIO-Erweiterung für Visual Studio Code
- Repository lokal geklont
- Internetverbindung beim ersten Build, damit PlatformIO die Frameworks und Bibliotheken laden kann

## Kompilieren

```powershell
pio run -e smalltv_clean
```

Das ESP8266-Binärimage liegt anschließend hier:

```text
.pio/build/smalltv_clean/firmware.bin
```

Lade die Datei über den manuellen Firmware-Upload der Geräte-Weboberfläche hoch. Für SmallTV Ultra gilt bei einer Erstinstallation gegebenenfalls der Loader-Weg aus [Firmware installieren](/smalltv-mod/getting-started/flashing/).

## Was das Clean-Ziel enthält

Das Image enthält WLAN, Geräteeinstellungen und Weboberfläche, Theme-Verwaltung und Renderer, Playlist, manuellen Firmware-Upload und Recovery. Es entfernt Börsen-/Krypto-Ticker, Nutzungsanzeige, Flugradar, Home Assistant/MQTT und Benachrichtigungen.

Die Playlist speichert bis zu acht Theme-IDs in Reihenfolge mit jeweils eigener Dauer von 5 bis 3600 Sekunden. Diese Werte werden zusammen mit der Gerätekonfiguration in `config.json` abgelegt. Die Firmware verwaltet Theme-Inhalte weiterhin als separate `.stheme`-Pakete.

## Recovery-Theme

Das Recovery-Theme wird separat in die Firmware eingebettet und erscheint nicht in der normalen Theme-Liste. Die Beispieldatei liegt unter [`examples/recovery-theme/recovery.stheme`](../../../../../examples/recovery-theme/recovery.stheme). Passe sie an und baue das Image erneut, um den Recovery-Bildschirm zu gestalten.

## GitHub Actions

Der Workflow `.github/workflows/build.yml` baut auch `smalltv_clean`. Bei erfolgreichem Lauf findest du das Artefakt im Actions-Lauf. Ein Release-Asset wird erst verfügbar, wenn ein Release mit dem Clean-Build veröffentlicht wurde.
