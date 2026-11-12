// RainPng.h — décodeur PNG embarqué pour les tuiles radar RainViewer.
#pragma once
#include <Arduino.h>
#include <FS.h>

// Fenêtre LZ du décodeur : 8192 pixels × 1 octet (indice kRainColors 6 bits
// + classe d'alpha 2 bits) = 8 Ko de heap. La profondeur de 8192 pixels
// couvre la fenêtre deflate native (32 Ko d'octets bruts ≈ 8184 pixels) :
// ne JAMAIS la réduire (89 % d'erreurs à 4096 px, vécu).
//
// Elle est allouée par l'appelant pour TOUT un fetch (les 13 frames), avant
// le réseau : à ce moment-là le heap est au plus propre, l'échec arrive
// avant qu'aucun fichier ne soit touché, et on évite 13 cycles malloc/free
// de 8 Ko qui fragmentaient le heap (vécu : après ~3 h d'uptime le plus
// gros bloc libre tombait sous 8192 et le fetch échouait en permanence).
#define RAIN_WINDOW_PIXELS 8192

// Décode `src` (tuile radar RainViewer : RGBA 8-bit, filtre « None » sur
// toutes les lignes — vérifié sur les tuiles réelles) vers `dst` (déjà ouvert
// en écriture, positionné à 0) : crop central cropK x cropK, upscale
// bilinéaire vers 240x240, quantifié en indices 8-bit dans kRainColors
// (240 octets/ligne, 57 600 octets au total), écrit séquentiellement. Les
// pixels sont prémultipliés sur noir avant quantification : le rendu est
// celui de RainViewer (validé PC : ≤0,15 % au-delà de ±12/canal vs un
// décodage zlib de référence, sur 6 tuiles live).
//
// `win` doit pointer sur RAIN_WINDOW_PIXELS octets. Il n'est pas réinitialisé
// entre les frames d'un même fetch : une back-référence deflate ne lit que
// des octets émis pendant le décodage courant (fenêtre 32 Ko), jamais des
// entrées périmées (prouvé par test2.cpp : deux décodages avec la même
// fenêtre non nettoyée produisent des sorties identiques).
//
// Retourne false sur toute erreur (PNG non conforme, deflate invalide,
// `win` null) ; `dst` est alors tronqué là où le décodage a cessé.
bool rainDecodeToFs(File& src, File& dst, int tileSize, int cropK, uint8_t* win);
