# CLAUDE.md — fork smalltv-mod : mode « Radar de pluie » (Rain)

Fork de [giovi321/smalltv-mod](https://github.com/giovi321/smalltv-mod) (licence WTFPL) vers `lanlanndn/smalltv-mod` : ajoute un **mode « rain »** (radar de pluie RainViewer, style RainToday) sur les écrans GeekMagic SmallTV. Remote `upstream` = giovi321, `origin` = lanlanndn.

**État (fin de session 12/11/2026)** : le mode rain est **implémenté, compilé, validé sur PC et sur l'Ultra, et validé visuellement par l'utilisateur** (photo : scope + timeline + nuages/pluie composites OK). Deux blocages de l'appareil ont été levés ce jour-là : le **malloc de la fenêtre LZ (8 Ko)** échouait (plus gros bloc 7232 o au décodage — LUT de couleurs déplacée en flash, +4 Ko de heap) et l'**alpha des nuages semi-transparents** était perdu dans les copies LZ (alpha 2 bits stocké avec l'indice). Un dernier bug de rendu (la frame effacée juste après le blit — « flash de nuages puis scope vide ») a été corrigé et re-validé à l'œil. Le diag temporaire `/api/diag` est **retiré**, le tout est committé et poussé. Plan détaillé initial : `/home/landry/.claude/plans/rustling-conjuring-dahl.md`.

Git : tout est poussé (`feat(rain)`, `docs(rain)`, `fix(rain)` ×2, dont le dernier commit de la session du 12/11 qui porte les correctifs décodeur/rendu).

## Matériel de l'utilisateur

- **Ultra** (la cible du mode rain) : `http://smalltv-2b41.local` / `192.168.1.24`. Flash **4 Mo vérifiée** (`ESP.getFlashChipSize()` = 4194304 — la théorie « puce 2 Mo » a été testée et écartée). WiFi : **Livebox-7D90** (attention : pas le Bytel-E-…-2.4GHz des vieilles notes). OTA stock d'origine = formulaire simple champ `firmware` (déjà remplacé par smalltv-mod v3.6.0 fork).
- **SDPro** : `smalltv-80c8.local` / `192.168.1.224`, smalltv-mod v3.5.1 **lean** (le build complet crashe cet appareil — heap). Injoignable le 11/11, à re-tester au besoin.
- Binaires de flash dans `~` : `~/smalltv-lean-firmware.bin` (rafraîchi le 12/11, build propre) et `~/smalltv-loader.bin` (315 920 o, du 10/11). Originaux : `.pio/build/smalltv_{loader,lean}/firmware.bin`.

## Faits techniques durs (ne pas re-vérifier — économise des heures)

### API RainViewer
- **Le radar n'est servi qu'au zoom 7** : z≥8 renvoie des tuiles de CARTE sans pluie (vérifié par analyse de pixels ; la doc « max zoom 7 » est appliquée). Tuiles utilisables = **256 px RGBA 8-bit, filtre PNG « None » sur 100 % des lignes** (re-vérifié sur les tuiles des 11-12/11).
- **Nowcast supprimé le 01/01/2026** (`radar.nowcast` toujours vide) — le code tolère les frames futures si elles reviennent.
- **HTTP clair fonctionne** (pas de TLS nécessaire) ; fallback `RAIN_USE_HTTPS` prévu dans `config.h` si RainViewer force un 301.
- URL image : `{host}{path}/256/7/{lat}/{lon}/2/1_1.png` (host = `tilecache.rainviewer.com` depuis `https://api.rainviewer.com/public/weather-maps.json` — **le host du JSON contient déjà `https://`**, le firmware le retire). Schéma 2 = Universal Blue.
- **Les tuiles contiennent AUSSI la couche nuageuse** (gris/bruns semi-transparents, alpha 0..255 — histogramme typique : beaucoup de 0/255 + un paquet intermédiaire 140-190 sur les nuages fins). C'est ce fond de nuages que l'utilisateur veut voir rendu fidèlement.
- Rate limit : 100 req/min/IP (on fait ~14 req/15 min). **Le fetch complet de 13 frames prend ~4,5 min sur l'Ultra** (décodage inclus) — l'appareil ne répond pas au HTTP pendant ce temps (serveur mono-tâche), ne pas conclure à un crash.

### Décodeur PNG custom (`src/features/rain/RainPng.cpp`) — validé sur PC et sur l'Ultra
- **PNGdec est inutilisable sur ESP8266** (~38 Ko de buffers statiques).
- Alternative : inflate minimal (RFC 1951) + **fenêtre LZ de 8192 pixels × 1 octet** = **8 Ko heap** : l'octet = **indice kRainColors (6 bits) + classe d'alpha (2 bits, {0,85,170,255})**. La profondeur de 8192 pixels couvre la fenêtre deflate native (32 Ko d'octets bruts ≈ 8184 pixels) — **ne JAMAIS réduire** : à 4096 pixels le décodage produit 89 % d'erreurs (le « carré bleu » vécu).
- **Pourquoi l'alpha dans la fenêtre** : le flux déflaté contient du RGBA brut ; les copies back-référence reconstruisent les canaux depuis l'entrée de fenêtre du pixel source. Sans alpha, une copie prenait alpha 255 et les **pixels mixtes** (littéral + copie — ~99 % du flux) étaient trop clairs ou trop sombres sur les nuages semi-transparents (écarts nets vs RainViewer, vécu). Reconstruction : canaux = `PAL[k] × 255/alpha` en facteurs entiers (×1, ×3/2, ×3), puis la prémultiplication de finishPixel redonne la couleur affichée. **La valeur vient du canal SOURCE (sch), la destination est le canal courant (chan)** — les confondre corrompt tout (bug vécu, testé par les tuiles à bandes synthétiques).
- **Raccourci « tout-histoire »** : un pixel dont les 4 octets viennent de la même entrée de fenêtre affiche directement `PAL[k]` sans re-quantifier — nécessaire car la LUT n'est pas idempotente sur les couleurs de la palette par construction naïve (voir LUT ci-dessous).
- **Prémultiplication alpha** : chaque pixel RGBA est prémultiplié sur noir (`x/255 ≈ (x + 1 + x>>8) >> 8`), le fond de l'écran. Les registres r/g/b sont des `uint16_t` (la reconstruction ×3 peut dépasser 255).
- **Quantification** : RGB565 prémultiplié → **LUT R5G5B2 de 4096 entrées EN FLASH** (`rain_clut.h`, générée par `python3 tools/make_rain_clut.py` — ne pas éditer à la main) → indice dans `kRainColors` (58 couleurs). La clé R5G5B2 préserve les gris sombres de la couche nuageuse — une clé RGB332 les écraserait tous dans un seul bin.
  - **Centres de bins** : `r5*8+4`, `g5*8+4` (la clé garde G5 = G6>>1), `b2*64+32`. Les anciens `g5*4+2`/`b2*64+36` écartaient la LUT des couleurs réelles (un bleu (0,145,202) tombait sur (0,125,180)) — ne pas réintroduire.
  - **Idempotence forcée** : `quantize(PAL[k]) = k`, en itérant la palette en ordre inverse (dans un bin partagé par des couleurs quasi identiques, la plus sombre gagne ; le noir garde le bin 0). Sans elle, un pixel affiché avec la couleur k retombait sur un voisin à la re-quantification.
  - `rainColorIndex` garde le raccourci `c == 0 → 0` (transparent).
  - La LUT en flash a libéré **4 Ko de BSS** : RAM 73,5 % (était 78,7 %), heap au boot ~14,3 Ko, **maxblk ~13,9 Ko** — le malloc de 8 Ko de la fenêtre passe avec ~4 Ko de marge (avant : free 7672 / blk 7232 au décodage, échec `fail 6`).
- **Format de frame : indices 8 bits**, 240×240 = **57 600 o/frame** (`/rain/fNN.raw`, 240 o/ligne). `MAX_RAIN_FRAMES` 13 (2 h de passé) = 748 800 o + tmp.png + meta ≈ 760 Ko sur LittleFS 1 Mo — ça tient, mais surveiller le pic transitoire pendant un fetch (nouvelles frames écrites avant purge des vieilles).
- **Validation PC** : harnais `tools/rain_fwtest/` dans le repo (README). Résultat de référence (12/11, 6 tuiles live) : **0,09–0,15 % au-delà de ±12/canal**, écart max 17-21 (bords de nuages interpolés entre couleurs quantifiées, ±1 pas de palette) ; une tuile synthétique constante donne 0 % exact. `test2.cpp` prouve le reset d'état (double décodage + sorties identiques). `groundtruth.py` (zlib + prémultiplication + bilinéaire 8.8 fixe + LUT répliquée) est la vérité terrain.
- **Pièges du décodeur déjà corrigés** (ne pas réintroduire) :
  - Les chunks **IDAT multiples ne sont pas contigus** dans le fichier (CRC + en-têtes intercalés) → le `BitSrc` saute les frontières via une liste de segments.
  - Ordre des bits : les codes huffman arrivent MSB-first ; lookup rapide 9 bits + chemin lent bit à bit avec état sauvegardé — consommer exactement `len` bits (un `acc >>= lbits` de trop avait tout cassé).
  - L'octet de filtre arrive **une fois par ligne** (`chan = -1` seulement si `pixels % w == 0`), pas par pixel.
  - **`QuantOut g_q` est statique : `begin()` doit remettre `pixels/chan/curRow/r/g/b/a/histE` à zéro** — sans ça le 2e décodage repart de pixels=65536 → compte 2×/3× (bug vécu, voir Bugs).
  - **Jamais de `pgm_read_word` (l16ui) depuis irom0** : LoadStoreError sur l'ESP8266 (crash au 1er rendu de l'Ultra). Toujours `memcpy_P` ou `pgm_read_byte` (les lectures octet fonctionnent).
  - Le callback PNGdec… n'existe pas ici — c'est mon inflate, pas de dépendance externe.
- **Mémoire** : les tables huffman (`g_huffLit/g_huffDist/g_huffClh`), `QuantOut g_q`, `g_srcBuf[512]` (BitSrc) et les `buf[240]` d'emitRow sont **statiques** — en locales ils feraient déborder la pile (marge cont stack mesurée : 688 B au repos, ~960-1264 B au décodage). Budget lean : **RAM 73,5 % (~21 Ko heap au repos, ~13-14 Ko à chaud), flash 68,2 %**.

### Rendu (`RainMode.cpp`)
- Géométrie reprise du radar avions : CX=120, CY=120, RR=112 ; « N » en haut au centre ; portée en haut à gauche ; heure de frame en haut à droite (HH:MM locale si `clockSynced()`, sinon relatif `-2h/-1h/now`) ; **timeline en bas** (ligne y=236, ticks 30 min, labels, curseur) ; attribution « RainViewer » en bas à droite ; point d'erreur rouge en bas à gauche.
- Frame = indices 8 bits blittés ligne par ligne (buffer statique 240 o) ; palette `g_pal[256]` construite depuis `kRainColors` (58 couleurs prémultipliées → RGB565, copiées par `memcpy_P` octet par octet — le `pgm_read_word` d'origine crashait) et passée par `gfxTint()` à chaque rendu.
- Villes : `rain_cities.h` **généré** (731 villes France/UE, geonames `cities5000` CC-BY 4.0) par `python3 tools/make_rain_cities.py [cities5000.txt]` — ne pas éditer à la main. Fonctionne sur l'appareil (le point « Nantes » s'est affiché avant le crash du rendu).
- Crop/upscale : K = round(2×range / kmPerPx) (z7 : kmPerPx = 40075·cos(lat)/32768), clampé 8..240 ; bilinéaire 8.8 fixe. À 50 km sur Nantes : cropK = 120.

### Couleurs (demande explicite de l'utilisateur)
- L'utilisateur veut **les rendus RainViewer tels quels, pas de palette réinterprétée**. `kRainColors` (58 entrées, `RainData.h`) = les couleurs RÉELLES des tuiles z7 (couche nuageuse gris/brun semi-transparente + rampe pluie Universal Blue), mesurées sur 6 tuiles live le 11/11 puis prémultipliées sur noir, triées sombre→clair. Index 0 = noir/aucune pluie.
- Si de nouvelles couleurs apparaissent (météo différente), la LUT mappe au plus proche voisin de la table (dégradation douce). Pour étendre la table : re-mesurer sur des tuiles fraîches (script de la session du 11/11), re-générer le tableau **et re-générer la LUT** (`python3 tools/make_rain_clut.py`).

## Bugs vécus et corrigés (détails pour ne pas refaire)

1. **Option « Rain » absente de l'UI Display** : `<option value="rain">` du sélecteur Mode et la case `carouselRain` manquaient dans `webui.html` (présentes dans MODEOPT/CAROPT mais jamais rendues). Sur le lean (usage/ha/theme compilés hors) il ne restait que Ticker et Radar. Corrigé : option + checkbox + `sc('carouselRain',…)` dans loadConfig. **Après toute édition de webui.html : `python3 tools/gzip_webui.py`** (le CI vérifie).
2. **Crash au premier rendu rain** : `Exception epc 0x4021d57e addr 0x4029a0e8`, **exccause 0x3 = LoadStoreError**, uptime ~34 s (le temps du premier fetch ~30 s + premier rendu). L'instruction fautive : `l16ui a2, a2, 0` = le `pgm_read_word(&kRainPalette[i])` de la boucle palette. Diagnostic : l'adresse de faute **suivait l'adresse de la palette à chaque build** (le linker place les données rain en fin d'irom0.text) ; les lectures octet/32 bits des mêmes adresses fonctionnaient ; le compilateur n'émettait de l16ui que dans cette boucle. **Cause : les loads 16 bits depuis la fenêtre cache irom0 lèvent LoadStoreError sur cet ESP8266.** Fix : `memcpy_P` (lecture octet) partout. Ne pas réintroduire de `pgm_read_word`.
3. **Décodage faux (« carré bleu »)** : la fenêtre LZ avait été réduite à 4096 pixels (8 Ko) pour le heap de l'Ultra → **89 % de pixels faux** (les back-réfs 8-32 Ko lisaient une histoire décalée). La validation « 90-95 % » d'origine avait été faite avec 8192 pixels. Restaurée à 8192, puis ré-implémentée **en indices 8 bits** (8 Ko heap au lieu de 16) avec le même résultat : 0 % d'écart.
4. **Compte de pixels 2×/3× sur l'appareil** (131 072 puis 196 608 au lieu de 65 536, multiples exacts) : `QuantOut g_q` est **statique** et `begin()` ne remettait pas l'état à zéro — les initialiseurs de membre ne jouent qu'à l'initialisation statique (1er décodage). Le PC ne le voyait jamais (un décodage par processus, d'où l'échappée). Fix : reset de `pixels/chan/curRow/r/g/b/a` dans `begin()`. Prouvé par `test2.cpp` (double décodage).
5. **OTA → DHCP en échec au reboot** (« Livebox-7D90: no ip (DHCP) » + AP SmallTV-Setup ouvert, ARP KO). Récupéré par l'utilisateur : connexion à l'AP SmallTV-Setup, re-saisie du mot de passe WiFi, reboot (reset « Software/System restart »). **Effet de bord : la config rain (mode/lat/lon/tz) est revenue aux défauts** → il a fallu re-POST `/api/config`. **Cause du DHCP jamais élucidée** (flash 4 Mo OK, image cohérente — vérifié par hash du fichier servi vs le bin local). Les 2 OTA du 12/11 n'ont PAS reproduit le bug. À surveiller : si « no ip (DHCP) », passer par l'AP et re-POST la config.
6. **Malloc de la fenêtre LZ en échec sur l'Ultra** (`stage: png decode failed`, diag `fail 6`, 0 pixel) : au moment du décodage le heap libre était **7672 o / plus gros bloc 7232 o < 8192 o** (le heap du lean est fragmenté par WiFi + web + fetchs). La fenêtre ne peut pas descendre sous 8192 pixels (bug 3). Fix : la LUT de quantification (4 Ko BSS) est **précalculée en flash** (`rain_clut.h`, générée par `tools/make_rain_clut.py`) → +4 Ko de heap. Vérifié sur l'appareil : `frames: 13`, `stage: ok`, heap stable ~13 Ko après fetch.
7. **Nuages semi-transparents mal rendus** (découvert le 12/11 par la tuile du jour, nuages fins) : jusqu'à 7,7 % des pixels au-delà de ±12/canal vs le rendu zlib de référence. Cause : la fenêtre ne stockait que l'indice de couleur — une copie LZ reconstruisait **alpha 255**, donc les pixels mixtes (littéral + copie) étaient trop clairs ou trop sombres. Fix : **classe d'alpha 2 bits {0,85,170,255} stockée avec l'indice** (1 octet/entrée), canaux reconstruits en brut (×255/alpha) puis prémultipliés. Résultat : ≤0,15 % au-delà de ±12/canal sur 6 tuiles live.
8. **Dérive d'un pas de palette sur les copies** : la LUT de quantification n'était **pas idempotente** sur les couleurs de la palette (53/58 retombaient sur un voisin) — un pixel copié affichait une couleur différente de la source. Fix : idempotence forcée dans `make_rain_clut.py` + raccourci « tout-histoire » dans `finishPixel` (affiche `PAL[k]` directement, sans re-quantifier).
9. **Corruption massive sur les tuiles à copies désalignées** (tuiles synthétiques à bandes) : `byteHist` utilisait le canal **source** pour choisir le registre de destination — un octet copié depuis un alpha écrivait dans le registre alpha au lieu de R/G/B. Fix : la valeur vient du canal source, la destination est le canal courant. Reproduit par les tuiles synthétiques du harnais (une tuile constante semi-transparente donne 0 % exact).
10. **La frame s'effaçait aussitôt blittée** (vu par l'utilisateur le 12/11 : flash de nuages ~50 ms puis scope vide, « pas de superposition nuages/pluie ») : `drawRain` appelait `blitFrame` puis `drawBase`, dont le `fillScreen(C_BLACK)` effaçait la frame juste blittée. Fix : `drawBase` (fond noir) → `blitFrame` → `drawScope` (cercles/croix/N par-dessus le champ). Ne pas réinverser.

## Suspicions / questions ouvertes

- **Rendu validé à l'œil le 12/11** : scope + timeline + nuages/pluie composites conformes (la session du 11/11 ne l'avait jamais vu fonctionner). Le juge de paix pour les prochaines modifs reste la photo utilisateur : couleurs RV, villes, timeline, superposition nuages/pluie.
- **Heap général** : RAM 73,5 %, heap ~13-14 Ko à chaud après fetch — marge OK. Si des crashs OOM apparaissent : candidats = réduire `MAX_RAIN_FRAMES` (13→10 = 576 Ko de FS), ou réduire `RAIN_FAST_BITS` 9→8 (+1,5 Ko de BSS).
- **LittleFS** : 13 frames × 57 600 = 748 800 o — le pic transitoire pendant un fetch (anciennes + nouvelles frames coexistantes) doit rester sous ~1 Mo. Si `RAIN_FS_FAIL` apparaît alors que le FS a d'autres fichiers : purger ou réduire MAX_RAIN_FRAMES.
- **DHCP post-OTA** (bug 5) : cause inconnue, non reproduite les 12/11. Pistes si ça se reproduit : tester un reboot simple (sans OTA) pour isoler, vérifier la table des baux de la Livebox.
- **L'OTA a peut-être « raté » une fois** (un build flashé ne présentait pas les champs diag attendus) — non expliqué, peut-être un faux souvenir de séquence. Si un comportement semble correspondre à une ancienne version : re-OTA et re-vérifier.

## Valider le décodage sur PC (harnais dans le repo)

`tools/rain_fwtest/` compile le RainPng.cpp firmware avec des stubs Arduino/FS minimaux (voir son README.md) :

```bash
cd tools/rain_fwtest
g++ -O2 -Dmemcpy_P=memcpy -I. -I../../src/features/rain test.cpp ../../src/features/rain/RainPng.cpp -o fwtest
./fwtest tuile.png sortie.raw 120            # sortie 57 600 o
g++ -O2 -Dmemcpy_P=memcpy -I. -I../../src/features/rain test2.cpp ../../src/features/rain/RainPng.cpp -o fwtest2x
./fwtest2x tuile.png sortie.raw 120          # double décodage : sorties identiques
python3 groundtruth.py tuile.png sortie.raw 120   # vs zlib+prémult+bilinéaire
```

`test2.cpp` vérifie le reset d'état de `g_q` (bug 2×/3×). Vérité terrain : zlib + prémultiplication + crop 120×120 + bilinéaire 240×240, comparer au .raw via kRainColors, tolérance ±12/canal (référence 12/11 : ≤0,15 % au-delà, écart max 17-21, sur 6 tuiles live ; 0 % sur une tuile synthétique constante semi-transparente). Tuiles de test : `weather-maps.json` puis `https://{host}{path}/256/7/47.151897/-1.608400/2/1_1.png` (le host du JSON contient déjà `https://`).

## Commandes

```bash
# Outillage ÉPINGLÉ (sinon les builds ESP32 échouent) :
#   pipx install --force platformio==6.1.19
#   + scons 4.8.1 dans le venv : ~/.local/share/pipx/venvs/platformio/bin/python -m pip install scons==4.8.1
#   (pio 6.2.0 → scons 4.11.1 → "ModuleNotFoundError: SCons.Tool.FortranCommon")

export PATH=$HOME/.local/bin:$PATH
pio run -e smalltv_lean                 # firmware ESP8266 (SDPro + Ultra)
pio run -e smalltv_loader               # loader 2 étapes (1re install Ultra)
pio run -e smalltv_esp32_8mb            # ⚠️ builder SEUL : le build c2 (RISC-V) avant
                                        #   pollue .pio/build/port/ (objet ed25519 RISC-V)
                                        #   → erreur "file in wrong format" au link
# Générés (ne pas éditer à la main) :
python3 tools/make_rain_clut.py         # rain_clut.h depuis kRainColors (RainData.h)
python3 tools/make_rain_cities.py [cities5000.txt]   # rain_cities.h
# Tests natifs (nécessitent ArduinoJson d'un env déjà buildé) :
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_lean/ArduinoJson/src sh tests/mode/run.sh
# Après TOUTE édition de src/webui.html :
python3 tools/gzip_webui.py            # régénère webui.h (le CI vérifie)
python3 tools/ci_checks.py assets      # cohérence UPDATE_ASSET ↔ dist/
```

## Intégration (points d'ancrage)

- `MODE_RAIN 7` + `WITH_RAIN 1` (reste actif dans le lean) + bloc `RAIN_*` : `src/config.h`
- Slice `RainSettings` + `carouselRain` + helpers `rainLat/rainLon` : `Settings.h/.cpp`
- Registre `kModes[]` + `carouselHas()` : `main.cpp` · statut : `StatusJson.h` (`writeStatusRain`) + `WebPortal.cpp` (`feat["rain"]`, bloc status, `handleRefresh`)
- Onglet web : `webui.html` (bouton `data-t="rain"`, section, `loadConfig`/`collect`, moteur i18n FR/EN `I18N` + `data-i18n` + localStorage `smalltv-lang`)
- Inclure `-iquote src/features/rain` : `platformio.ini`

## Flash (procédure de reprise pour l'Ultra)

1. Brancher l'Ultra, vérifier la joinabilité : `curl -s http://192.168.1.24/api/status` (ou mDNS `smalltv-2b41.local`). S'il ne répond pas : attendre **~5 min** (le fetch rain complet dure ~4,5 min, serveur mono-tâche) avant de conclure.
2. OTA du build corrigé : `curl -s -F "update=@~/smalltv-lean-firmware.bin" http://192.168.1.24/update` (ou `.pio/build/smalltv_lean/firmware.bin`). Réponse attendue `OK`. Les frames sur LittleFS survivent à l'OTA : le radar s'affiche dès le boot suivant (pas besoin d'attendre le prochain poll).
3. **Si « no ip (DHCP) » après le reboot** (bug 5) : rejoindre l'AP **SmallTV-Setup**, re-saisir le mot de passe WiFi (Livebox-7D90), laisser le device rebooter, puis **re-POST la config rain** (elle revient aux défauts).
4. Config attendue (via API, fichier JSON — le `^` de ^GSPC et les `$` imposent `--data-binary @fichier`) :
   ```json
   {"mode": "rain",
    "rain": {"lat":47.1519,"lon":-1.6084,"useDevice":false,"rangeKm":50,"pollSec":900,"animSec":8,"unitsMi":false,"showCities":true},
    "clock": {"tz":"Europe/Paris","tzPosix":"CET-1CEST,M3.5.0,M10.5.0/3"}}
   ```
   Vérifier la persistance : GET `/api/config` (la sauvegarde est synchrone en flash).
5. **Valider le rendu** (photo utilisateur) : fond radar, villes (Nantes visible), nuages RV gris/brun, pas de « Waiting for radar data » (frames présentes), timeline, pas de crash. Statut : `rain.frames = 13`, `stage: ok`, `heap` stable (~13 Ko après fetch), `reset: Software/System restart`.

## Pièges récurrents (vécus)

- **Page web restée ouverte** avec un état périmé → chaque Save écrase les changements API. Toujours F5 avant de modifier.
- **Appareil injoignable pendant ~5 min après un changement ou un reboot = fetch rain en cours** (serveur mono-tâche) — attendre, ne pas bombarder (des curl en rafale peuvent aussi ralentir le fetch).
- **Ne jamais flasher le build complet (`smalltv`) sur le SDPro** (crash Yahoo, heap 16 Ko). Lean uniquement.
- `F()` n'accepte que des littéraux — pas de ternaire dedans (erreur `__pstr__`).
- `Stream::read(buf, len)` n'existe pas sur l'ESP32 core 3.x → utiliser `readBytes` (déjà corrigé dans `RainClient.cpp`).
- Le hook `impeccable` signale une hiérarchie typo plate dans webui.html — **intentionnel** (style upstream, ne pas « corriger »).
- **UI web** : ne pas re-supprimer l'option « Rain » du sélecteur Mode ni la case `carouselRain` (ajoutées le 11/11).
- **`pgm_read_word` depuis irom0 → l16ui → LoadStoreError** (crash au 1er rendu rain). Toujours `memcpy_P`/`pgm_read_byte`.
- **Fenêtre LZ du décodeur : 8192 pixels minimum** (8 Ko heap en 1 octet/entrée). Réduire casse le décodage (89 % d'erreurs à 4096).
- **`QuantOut g_q` statique** : `begin()` doit remettre l'état à zéro, sinon le 2e décodage compte 2× les pixels.
- **OTA sur l'Ultra** : DHCP peut échouer au reboot suivant (récupération via AP SmallTV-Setup + re-POST de la config).
- **`rain_clut.h` et `rain_cities.h` sont générés** — toute modif de `kRainColors` doit être suivie de `python3 tools/make_rain_clut.py`.
