---
title: Themes und Playlist
description: .stheme-Pakete installieren und mit individueller Dauer als Wiedergabeliste anzeigen.
---

Das Clean Image enthält eine Theme-Engine für installierbare `.stheme`-Pakete. Ein Theme beschreibt, wie Inhalte auf dem 240 × 240 Pixel großen Display angeordnet und gezeichnet werden. So kannst du Zifferblätter, Büroanzeigen und persönliche Statusansichten wechseln, ohne die Firmware neu zu bauen.

## Unterstützte Theme-Inhalte

Der Theme-Renderer unterstützt die im Paketformat definierten Ebenen, darunter Bitmap-Bilder, die eingebaute ASCII-Bitmap-Schrift, Uhrzeit und Datum, Text, einfache Formen und Sprite-Animationen. GIFs und zusätzliche Font-Dateien werden nicht unterstützt. Ein Theme ist eine Datendatei und führt keinen eigenen Programmcode aus.

## Theme installieren und auswählen

1. Öffne im Browser den Bereich **Anzeige**.
2. Wähle eine `.stheme`-Datei aus und klicke auf **.stheme installieren**.
3. Das installierte Theme kannst du direkt anzeigen oder der Playlist hinzufügen.
4. Die Theme-Pakete liegen getrennt von der Firmware im Gerätespeicher.

Das einzeln ausgewählte Theme dient als Anzeige, wenn die Playlist ausgeschaltet ist, und als Rückfall, wenn kein Playlist-Eintrag geladen werden kann.

## Playlist konfigurieren

Im Bereich **Anzeige → Theme-Playlist** kannst du bis zu acht installierte Themes aufnehmen. Verwende die Pfeile, um die Reihenfolge zu ändern. Jeder Eintrag besitzt eine eigene Anzeigedauer zwischen **5 und 3600 Sekunden**. Aktiviere anschließend die Playlist und speichere die Einstellungen.

Die Firmware speichert Aktivierung, Reihenfolge und Zeiten in der Konfiguration. Beim Wechsel lädt sie das nächste Theme und zeigt es für die festgelegte Zeit. Nicht mehr installierte oder ungültige Themes werden übersprungen. Ist kein gültiger Playlist-Eintrag übrig, erscheint das einzeln ausgewählte Rückfall-Theme.

## Recovery-Theme

Das `recovery.stheme` ist eine besondere Datei, die beim Build in die Firmware eingebettet wird. Sie ist unabhängig von den installierten Benutzer-Themes und wird nach wiederholten Abstürzen vom Recovery-Schutz verwendet. Sie erscheint nicht in der Playlist. Ein Beispiel liegt unter [`examples/recovery-theme/recovery.stheme`](../../../../../examples/recovery-theme/recovery.stheme).

## Speicher und Grenzen

Der ESP8266 verfügt über begrenzten RAM und Flash. Die Firmware hält die Playlist deshalb klein (maximal acht Einträge) und speichert nur Theme-IDs, Reihenfolge und Zeiten in den Einstellungen; die Theme-Dateien bleiben im Dateisystem. Die tatsächliche Anzahl installierbarer Pakete hängt von deren Größe und dem freien Flash-Speicher ab.

## Paketformat und Beispiele

Im Verzeichnis [`examples/themes/`](../../../../../examples/themes/) liegen Theme-Beispiele und Quellmaterial. Die Hilfsprogramme und Formatdetails befinden sich im Repository unter `tools/` und `tests/theme/`. Dieses Clean Image lässt die Theme-Engine vollständig aktiviert.
