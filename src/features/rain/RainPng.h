// RainPng.h — décodeur PNG embarqué pour les tuiles radar RainViewer.
#pragma once
#include <Arduino.h>
#include <FS.h>

// Décode `src` (tuile radar RainViewer : RGBA 8-bit, filtre « None » sur
// toutes les lignes — vérifié sur les tuiles réelles) vers `dst` (déjà ouvert
// en écriture, positionné à 0) : crop central cropK x cropK, upscale
// bilinéaire vers 240x240, quantifié en nibbles 4-bit (2 px/octet, 120
// octets/ligne, 28 800 octets au total), écrit séquentiellement.
//
// Le décodeur évite la fenêtre deflate de 32 Ko : sans dé-filtrage, la
// fenêtre LZ stocke du RGB565 par pixel (16 Ko heap, allouée le temps du
// décodage puis libérée). Les tables huffman (~3 Ko) sont transitoires.
// Les back-références désalignées sur les pixels mélangent les canaux de
// deux pixels sources — sans dé-filtrage ni CRC, c'est un léger lissage
// (validé sur PC : 90-92 % des pixels identiques à un décodage exact).
//
// Retourne false sur toute erreur (PNG non conforme, deflate invalide,
// mémoire insuffisante) ; `dst` est alors tronqué là où le décodage a cessé.
bool rainDecodeToFs(File& src, File& dst, int tileSize, int cropK);
