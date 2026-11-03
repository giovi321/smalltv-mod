# SmallTV Clean Image

**Ein schlankes, themenbasiertes Firmware-Image für GeekMagic SmallTV und SmallTV Ultra mit ESP8266.** Projektinhaber und Maintainer dieses Forks: **Jeff-Amdg** (GitHub).

Dieses Projekt ist der von **Jeff-Amdg** gepflegte Fork von [smalltv-mod](https://github.com/giovi321/smalltv-mod). Es richtet die ESP8266-Firmware auf einen klaren Zweck aus: Das Display soll mit installierbaren `.stheme`-Designs als Büro-Uhr, Statusanzeige oder persönliches Mini-Dashboard dienen. Die Firmware liefert dafür WLAN, Weboberfläche, Theme-Engine, Playlist, Displaysteuerung und Wiederherstellung. Inhalte und Gestaltung kommen überwiegend aus Themes, sodass ein neues Zifferblatt keine neue Firmware erfordert.

## Was im Image enthalten ist

- **`.stheme`-Theme-Engine:** Themes installieren, auswählen und löschen. Die Firmware rendert unterstützte Bitmap-Grafiken, ASCII-Bitmap-Schrift, Uhrzeit, Datum, Text, Formen und einfache Animationen gemäß dem Theme-Format.
- **Theme-Playlist:** Bis zu acht installierte Themes in einer gespeicherten Reihenfolge automatisch anzeigen. Für jeden Eintrag lässt sich die Anzeigedauer getrennt einstellen (5 bis 3600 Sekunden). Fehlende oder ungültige Themes werden übersprungen.
- **WLAN und Weboberfläche:** Erstinstallation über den `SmallTV-Setup`-WLAN-Hotspot, danach Konfiguration im Browser über Gerätenamen oder IP-Adresse. Unterstützt 2,4-GHz-WLAN.
- **Displaysteuerung:** Helligkeit, Ausrichtung, Farbkorrektur sowie Zeitzone und Nachtabschaltung konfigurieren.
- **Firmwarepflege:** Firmware manuell über die Weboberfläche hochladen; Export und Import der Geräteeinstellungen.
- **Recovery:** Nach wiederholten Abstürzen startet ein Schutzmodus. Ein separates, in die Firmware eingebettetes `recovery.stheme` wird angezeigt und bleibt von der normalen Theme-Auswahl getrennt.

Ticker, Krypto-/Börsenkurse, Claude-Nutzungsanzeige, Flugradar, Home Assistant/MQTT und Benachrichtigungen sind aus diesem Image entfernt, um Funktionen und Speicherbedarf auf den Theme-Einsatz auszurichten. Das Clean Image baut für **ESP8266** (SmallTV und SmallTV Ultra); es ist kein Image für ESP32-Varianten.

## Playlist verwenden

1. Öffne die Geräteoberfläche und gehe zu **Display**.
2. Installiere zuerst `.stheme`-Dateien im Bereich **Themes**.
3. Füge die gewünschten installierten Themes der Playlist hinzu.
4. Sortiere die Einträge mit den Pfeilen und trage pro Theme eine Dauer in Sekunden ein.
5. Aktiviere die Playlist und speichere. Die Reihenfolge und Zeiten werden in der Gerätekonfiguration gesichert.

Wenn die Playlist deaktiviert ist, zeigt die Firmware das einzeln ausgewählte Theme. Ist kein Playlist-Eintrag mehr gültig, fällt die Anzeige auf dieses ausgewählte Theme zurück.

## Installation

### SmallTV mit ESP8266

Wenn die vorhandene Firmware einen manuellen Upload unterstützt, öffne deren Update-Seite, lade `smalltv-clean-firmware.bin` hoch und warte auf den Neustart. Die Clean-Firmware stellt anschließend den Hotspot `SmallTV-Setup` bereit, falls noch kein WLAN konfiguriert ist.

### SmallTV Ultra

Das Ultra verwendet ebenfalls einen ESP8266, aber die originale Ultra-Firmware reserviert einen großen Teil des Flash-Speichers für eigene Inhalte. Daher kann ihr Update-Dialog ein vollständiges Firmware-Image mit „Not Enough Space“ ablehnen. Für die Erstinstallation wird in diesem Fall einmalig der vorhandene SmallTV-Loader benötigt. Danach kann das Image über die Clean-Weboberfläche manuell aktualisiert werden. Prüfe vor dem Flashen Modell und Installationsweg; ein falsches Image kann das Gerät unbrauchbar machen.

Nach dem Flashen mit dem WLAN `SmallTV-Setup` verbinden. Wenn das Einrichtungsportal nicht automatisch öffnet, `http://192.168.4.1` aufrufen, das eigene 2,4-GHz-WLAN eintragen und danach die im Display angezeigte Geräteadresse im Browser öffnen.

## Image bauen

Voraussetzung: PlatformIO Core oder die PlatformIO-Erweiterung für VS Code.

```powershell
pio run -e smalltv_clean
```

Das fertige Image liegt hier:

```text
.pio/build/smalltv_clean/firmware.bin
```

GitHub Actions baut dasselbe Ziel. Ein veröffentlichtes Release-Image erscheint als `smalltv-clean-firmware.bin`. Bis ein solches Release vorhanden ist, kann das lokal gebaute `firmware.bin` über den manuellen Firmware-Upload installiert werden.

## Projekt, Maintainer und Beiträge

- Maintainer und Projektinhaber dieses Forks: [Jeff-Amdg auf GitHub](https://github.com/Jeff-Amdg)
- Quellcode und Änderungen: [Jeff-Amdg/smalltv-mod auf GitHub](https://github.com/Jeff-Amdg/smalltv-mod)
- Grundlage und ursprüngliches Projekt: [giovi321/smalltv-mod](https://github.com/giovi321/smalltv-mod)
- Theme-Dateien und Recovery-Beispiel: [`examples/`](examples/)

Fehlerberichte sollten das genaue SmallTV-Modell, den installierten Image-Namen und die Schritte zur Reproduktion enthalten. Bitte keine WLAN-Passwörter oder privaten Konfigurationsdateien veröffentlichen.

## Lizenz

Die Lizenzbedingungen des zugrunde liegenden Projekts stehen in [LICENSE](LICENSE). Theme-Dateien können eigene Urheber- und Lizenzangaben haben.
