// RainPng.h — décodeur PNG embarqué pour les tuiles radar RainViewer.
#pragma once
#include <Arduino.h>
#include <FS.h>

// Décode `src` (tuile radar RainViewer : RGBA 8-bit, filtre « None » sur
// toutes les lignes — vérifié sur les tuiles réelles) vers `dst` (déjà ouvert
// en écriture, positionné à 0) : crop central cropK x cropK, upscale
// bilinéaire vers 240x240, quantifié en indices 8-bit dans kRainColors
// (240 octets/ligne, 57 600 octets au total), écrit séquentiellement. Les
// pixels sont prémultipliés sur noir avant quantification : le rendu est
// celui de RainViewer (validé PC : ≤0,15 % au-delà de ±12/canal vs un
// décodage zlib de référence, sur 6 tuiles live).
//
// Le décodeur évite la fenêtre deflate de 32 Ko : sans dé-filtrage, la
// fenêtre LZ stocke par pixel l'indice kRainColors (6 bits) + une classe
// d'alpha (2 bits) — 8 Ko de heap, alloués le temps du décodage puis
// libérés. L'alpha en histoire préserve les nuages semi-transparents : sans
// lui, les pixels mixtes (littéral + copie) étaient trop clairs ou trop
// sombres. La LUT de quantification (4096 entrées) vit en flash
// (rain_clut.h, générée par tools/make_rain_clut.py).
//
// Retourne false sur toute erreur (PNG non conforme, deflate invalide,
// mémoire insuffisante) ; `dst` est alors tronqué là où le décodage a cessé.
bool rainDecodeToFs(File& src, File& dst, int tileSize, int cropK);
