---
title: Einstellungen
description: WLAN, Display, Themes, Playlist und Systempflege im SmallTV Clean Image.
---

Die Geräteeinstellungen öffnest du im Browser über die IP-Adresse, die auf dem Display steht. Änderungen werden im Gerät gespeichert; manche Anzeigeoptionen greifen direkt, ein Wechsel des WLANs löst einen Neustart aus.

## Status

Zeigt Firmware-Variante, WLAN-Verbindung, IP-Adresse, Signalstärke, freien Speicher und Laufzeit. Diese Werte helfen auch bei einer Fehlersuche.

## WiFi

Suche nach einem 2,4-GHz-Netzwerk und speichere die Zugangsdaten. Du kannst den Gerätenamen ändern und damit die lokale Adresse leichter wiederfinden. Der ESP8266 unterstützt kein 5-GHz-WLAN.

## Display

Hier stellst du die Helligkeit und unterstützte Ausrichtungs- und Farboptionen ein. Der Theme-Modus zeigt entweder das direkt ausgewählte Theme oder – wenn aktiviert – die Playlist.

### Theme-Playlist

- **Aktivieren:** Schaltet die automatische Wiedergabe ein oder aus.
- **Einträge hinzufügen:** Wähle installierte Themes aus. Die Playlist kann bis zu acht Einträge enthalten.
- **Reihenfolge:** Verwende die Pfeile neben einem Eintrag.
- **Anzeigedauer:** Gib für jedes Theme Sekunden zwischen 5 und 3600 ein.
- **Speichern:** Sichert Aktivierung, Reihenfolge und Zeiten in der Gerätekonfiguration.

Fehlt ein Theme auf dem Gerät oder kann es nicht geladen werden, überspringt die Firmware diesen Eintrag. Falls kein gültiger Eintrag übrig ist, wird das einzeln ausgewählte Theme als Rückfallanzeige verwendet.

## Themes

Installiere `.stheme`-Pakete, wähle das einzelne Rückfall-Theme aus oder entferne nicht mehr benötigte Pakete. Das separate Recovery-Theme ist in der Firmware eingebettet und taucht hier nicht auf.

## System

Konfiguration als Datei sichern oder aus einer Sicherung laden, manuell eine Firmwaredatei hochladen und das SmallTV neu starten. Behandle Sicherungsdateien vertraulich, da sie WLAN-Zugangsdaten enthalten können.
