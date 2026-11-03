---
title: Fehlerbehebung
description: Hilfe bei WLAN, Themes, Playlist und Firmware-Upload im Clean Image.
---

## Das SmallTV-Setup-WLAN erscheint nicht

Warte nach dem Einschalten etwa eine Minute und starte das Gerät dann einmal neu. Das Setup-WLAN wird vor allem dann gestartet, wenn noch kein WLAN gespeichert ist oder keine gespeicherte Verbindung hergestellt werden kann. Achte darauf, dass dein Router 2,4 GHz aktiviert hat.

## Ich finde die Weboberfläche nicht

Lies die IP-Adresse vom Display ab und öffne sie mit `http://` im Browser. Laptop oder Smartphone müssen im selben lokalen Netzwerk wie das SmallTV sein. Falls die IP nicht erreichbar ist, starte Router und SmallTV neu oder verbinde dich erneut mit `SmallTV-Setup` und öffne `http://192.168.4.1`.

## Ein Theme wird nicht angezeigt

Prüfe, ob das `.stheme`-Paket im Bereich **Themes** als installiert erscheint und ob es zum unterstützten Theme-Format passt. Wähle es zunächst direkt als einzelnes Theme aus. Ein beschädigtes oder nicht unterstütztes Paket kann nicht geladen werden.

## Die Playlist wechselt nicht

Öffne **Display** und prüfe, ob die Playlist aktiviert und gespeichert ist. Sie benötigt mindestens einen installierten Eintrag. Die Dauer jedes Eintrags muss zwischen 5 und 3600 Sekunden liegen. Entfernte oder nicht ladbare Themes werden übersprungen; wenn kein gültiger Eintrag verbleibt, greift das ausgewählte Einzel-Theme.

## Der Upload sagt „Not Enough Space“

Das tritt vor allem beim SmallTV Ultra mit originaler Firmware auf. Obwohl das Gerät ESP8266 und 4 MB Flash hat, kann seine ursprüngliche Aufteilung einen zu kleinen OTA-Bereich für das Clean Image lassen. Verwende für die Erstinstallation den SmallTV-Loader. Sobald das Gerät mit dem Clean Image läuft, aktualisierst du es über dessen manuellen Upload.

## Das Gerät startet wiederholt neu

Der Boot-Schutz zählt aufeinanderfolgende Abstürze und startet bei wiederholten Fehlern im Recovery-Modus. Dort kann das separate `recovery.stheme` erscheinen. Die normale Theme-Auswahl bleibt erhalten. Wähle ein bekannt funktionierendes Theme und starte neu. Wenn das Problem bleibt, installiere ein geprüftes Image erneut.

## Wo melde ich einen Fehler?

Öffne ein Issue im [GitHub-Projekt](https://github.com/Jeff-Amdg/smalltv-mod/issues) und nenne das Gerätemodell, den Image-Namen, die sichtbare Fehlermeldung und die Schritte, mit denen der Fehler auftritt. Hänge keine Konfigurationsdatei mit WLAN-Passwort an.
