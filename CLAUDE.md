# CLAUDE.md — guide pour les agents IA

Lis ce fichier en entier avant toute action. Réponds à l'utilisateur **en français**.

## 1. Le projet en bref
Firmware alternatif pour un **GeekMagic SmallTV-Ultra** : petit écran connecté
(ESP8266 + écran ST7789 240×240). Le propriétaire veut à terme son propre firmware
(« son OS ») avec ses propres fonctions d'affichage.

Ce que le dépôt fournit :
- le **code source** ;
- des **Releases** contenant un zip (`.bin` précompilé + MD5 + notice) à installer
  **par la page web `/update`** de l'écran, comme les mises à jour officielles GeekMagic.

Ce que le dépôt ne fournit **pas** : de procédure de câblage ou de flashage manuel par port
série. Ne pas en ajouter. Le propriétaire dispose d'une solution de secours privée, hors dépôt.

Contraintes fondatrices :
- Installation et mises à jour **uniquement par le Wi-Fi** (`/update`).
- **Retour au firmware d'origine toujours possible** par cette même page.
- Les fichiers d'origine sur l'appareil (images, réglages, Wi-Fi) restent intacts.

## 2. État actuel (passation)
- L'appareil du propriétaire fait tourner **Custom-0.1.0** (validé : écran, couleurs,
  rétroéclairage, Wi-Fi, interface web, `/update`).
- Le code du dépôt est en **0.2.0** (voir CHANGELOG.md) : compilé avec succès (~376 Ko),
  **pas encore installé** sur l'appareil, **aucune Release publiée** pour l'instant.
- Le workflow GitHub Actions n'a **jamais tourné** : le surveiller au premier push.
- Aucune fonction « métier » encore (horloge, météo, images…) : à définir avec le propriétaire.

## 3. Règles absolues (ne jamais enfreindre)
1. **Tout firmware doit garder `/update`**, accessible en Wi-Fi normal **et** en point
   d'accès de secours. Sans lui, l'appareil n'est plus récupérable sans l'ouvrir.
   Ne jamais bloquer `loop()` longtemps (le serveur web doit continuer de répondre).
2. **Ne jamais formater LittleFS** : garder `cfg.setAutoFormat(false)`, ne pas appeler
   `LittleFS.format()`, ne pas écraser/supprimer les fichiers d'origine (liste dans
   `docs/stock-firmware.md`). Nos propres fichiers : nom dédié (`/custom.json`…).
3. **Garder le découpage flash** `board_build.ldscript = eagle.flash.4m3m.ld`.
4. **Taille du `.bin` < 520 000 octets** (vérifié par `build.*` et `make_release.py`).
   Au-delà, il ne peut plus être installé depuis le firmware d'origine (540 672 o libres),
   et il doit laisser ≥ 505 200 o libres pour un retour à l'origine.
5. **Ne jamais envoyer un firmware sur l'appareil, créer un tag ou publier une Release
   sans l'accord explicite du propriétaire.**
6. Ne jamais forcer GPIO0 ou GPIO2 à l'état bas au démarrage (broches de boot de l'ESP8266).
7. Ne pas réafficher le formulaire « FileSystem » sur `/update` (il effacerait toute la
   zone de fichiers).

## 4. Confidentialité (dépôt public)
- **Ne rien committer de personnel** : nom, e-mail, nom d'utilisateur, chemins
  `C:\Users\…` ou `/home/…`, SSID/mot de passe Wi-Fi, adresse MAC, IP du propriétaire,
  infos sur son réseau, ses serveurs ou son matériel annexe.
- Exemples génériques uniquement (`<ip-de-l-ecran>`, `<nom>`).
- **Jamais de `.bin` dans le dépôt** : nos binaires vont dans les Releases ; les firmwares
  GeekMagic (propriétaires) et les images flash personnelles ne sont jamais publiés.
  Le `.gitignore` les exclut ; ne pas le contourner.
- Ne pas reproduire les chaînes ressemblant à des clés API trouvées dans le firmware d'origine.
- Avant chaque commit : relire `git diff --cached` pour vérifier l'absence de ces informations.

## 5. Arborescence
```
platformio.ini               compilation (versions figées) + config écran TFT_eSPI (build_flags)
src/main.cpp                 tout le firmware (un seul fichier pour l'instant)
tools/setup.ps1 | setup.sh   crée .venv avec PlatformIO (local au projet)
tools/build.ps1 | build.sh   compile + vérifie la limite de taille
tools/make_release.py        fabrique dist/SmallTV-Ultra-Custom-<version>.zip
tools/upload.ps1             envoie un .bin via /update (curl), vérifie /v.json avant/après
tools/check_firmware.py      vérifie le CRC Arduino d'un .bin (et extrait un firmware d'une image flash)
tools/analyze_firmware.py    analyse un firmware binaire (segments, pages gzip, chaînes)
docs/hardware.md             matériel, brochage, découpage flash, contrainte de taille
docs/stock-firmware.md       API et fichiers du firmware d'origine
docs/recovery.md             retour à l'origine, Wi-Fi de secours, erreurs de mise à jour
.github/workflows/build.yml  CI : build à chaque push, Release sur tag vX.Y.Z
```

## 6. Commandes
Windows (PowerShell 5.1) :
```powershell
.\tools\setup.ps1                 # une fois
.\tools\build.ps1                 # -> .pio\build\smalltv-ultra\firmware.bin
py tools\make_release.py          # -> dist\SmallTV-Ultra-Custom-<version>.zip
.\tools\upload.ps1 -Ip <ip>       # installe (avec accord du propriétaire !)
```
Linux :
```bash
bash tools/setup.sh && bash tools/build.sh && python3 tools/make_release.py
curl -F "firmware=@.pio/build/smalltv-ultra/firmware.bin" http://<ip>/update   # avec accord !
```
- La toolchain est dans `.pio-core` (`PLATFORMIO_CORE_DIR`, géré par les scripts build).
- Version en cours sur l'écran : `GET http://<ip>/v.json`.
- Mode secours de l'appareil : réseau `SmallTV-Custom`, IP `192.168.4.1`.
- Un agent dans une VM Linux sans accès au réseau local de l'écran peut compiler et préparer
  la release, mais pas installer : c'est le propriétaire qui envoie le `.bin`.

## 7. Publier une version
1. Modifier le code, incrémenter `-D FW_VERSION=\"Custom-x.y.z\"` dans `platformio.ini`,
   compléter `CHANGELOG.md`.
2. Compiler, puis tester sur l'appareil avec l'accord du propriétaire (écran, `/`, `/update`,
   mode secours si le Wi-Fi est concerné).
3. Avec son accord : commit, puis tag `vx.y.z` (identique à FW_VERSION) et push du tag.
   GitHub Actions compile et publie la Release avec le zip. Sinon, joindre à la main le zip
   produit par `make_release.py`.

## 8. Matériel (résumé — détails dans docs/hardware.md)
| Élément | Valeur | Statut |
|---|---|---|
| SoC / flash | ESP8266EX 26 MHz, 4 Mo DIO | vérifié |
| Écran | ST7789 240×240, SPI **mode 3**, 40 MHz ; MOSI 13, SCLK 14, DC 0, RST 2, CS non câblé | vérifié |
| Couleurs | ordre RVB correct, pas d'inversion à ajouter | vérifié |
| Rétroéclairage | GPIO5, PWM **inversé** (`blInverted = true` par défaut) | vérifié |
| Bouton / tactile | **aucun** — ne pas en supposer un (erreur déjà commise) | vérifié |
| Autres GPIO | inconnus | ne rien supposer |

L'appareil ne se pilote donc que par le réseau (interface web, API, données récupérées en ligne).

## 9. Architecture de `src/main.cpp`
Démarrage (`setup`) :
1. Monte LittleFS **sans formatage** → charge `/custom.json` (`brt`, `blinv`).
2. Rétroéclairage PWM (plage 0–1023, 1 kHz), init écran, écran « Connexion Wi-Fi… ».
3. `startWifi()` : identifiants mémorisés par le SDK → sinon `/config.json` d'origine
   (`{"a":ssid,"p":mdp}`) → sinon point d'accès ouvert `SmallTV-Custom` (20 s par essai).
4. `setupWeb()` puis écran final : IP de l'interface, ou consignes du mode secours.

Boucle (`loop`) : `server.handleClient()` ; en mode secours sans client pendant 5 min →
redémarrage (nouvel essai Wi-Fi, utile après une coupure de courant).

Routes HTTP :
| Route | Rôle |
|---|---|
| `GET /` | page d'état + réglages |
| `GET /set?brt=0..100` / `?blinv=toggle` | luminosité / polarité du rétroéclairage (sauvés dans `/custom.json`) |
| `GET /wifi`, `POST /wifi` | scan + choix du réseau (stocké dans la zone Wi-Fi du SDK), puis redémarrage |
| `GET /update` | notre page (firmware uniquement) — **déclarée avant** `updater.setup()` car le serveur prend le premier handler qui correspond |
| `POST /update` | traitement `ESP8266HTTPUpdateServer` (champ `firmware`) |
| `GET /reboot` | redémarrage |
| `GET /v.json` | `{"m":"SmallTV-Ultra","v":"Custom-x.y.z"}` (même format que l'origine) |
| autre | 404, ou redirection vers `http://192.168.4.1/` en mode secours (portail captif) |

## 10. Conventions
- Textes affichés sur l'écran : **sans accents** (polices GLCD/Font2/Font4 de TFT_eSPI
  en ASCII). Utiliser `drawFit()` pour les textes de longueur variable.
- Interface web et commentaires : en français, sans accents, comme le code existant.
- Chaînes constantes avec `F("…")` pour économiser la RAM (~80 Ko au total, ~43 Ko libres).
- Garder les versions figées dans `platformio.ini` ; une mise à jour de plateforme ou de
  bibliothèque est un changement à tester sur l'appareil et à noter dans le CHANGELOG.
- Avant de proposer une installation : compiler, vérifier la taille, relire les règles §3.

## 11. Pièges connus
- Le checksum XOR classique des images ESP8266 ne correspond pas sur les binaires Arduino :
  `elf2bin.py` écrit taille + CRC à `0x1010`/`0x1014`. Utiliser `tools/check_firmware.py`.
- La page `/update` de `ESP8266HTTPUpdateServer` affiche par défaut un formulaire
  « FileSystem » : d'où notre page GET personnalisée.
- `WiFi.scanNetworks()` en mode point d'accès active temporairement le mode station.
- Sur le firmware d'origine, `/config.json` via HTTP masque le mot de passe (`"p":"****"`),
  mais le fichier dans LittleFS le contient en clair.
- Envoi de fichiers par le site github.com : le `.gitignore` n'est pas appliqué.
- **Windows : chemin du projet court obligatoire** ; le compilateur Xtensa ne gère pas les
  chemins longs (`fatal error: bits/c++config.h: No such file or directory`). Ne pas non
  plus remplacer `.venv` / `.pio-core` par des jonctions.
- PowerShell 5.1 : pas de `$ErrorActionPreference = "Stop"` autour des commandes natives
  (pio, pip, curl) — leur stderr devient une erreur fatale quand la sortie est capturée.
  Tester `$LASTEXITCODE`. Pas de `&&` : utiliser `;` et `if ($?) { … }`.
- Les scripts `.sh` doivent rester en fins de ligne LF (`.gitattributes`) ; les lancer
  avec `bash tools/xxx.sh` (le bit exécutable peut manquer).

## 12. Pistes pour la suite (à valider avec le propriétaire)
- Horloge NTP, météo (Open-Meteo ne demande pas de clé), affichage d'images/GIF depuis LittleFS.
- Réutiliser les fichiers d'origine (images de `/image/`, GIF de `/gif/`) en lecture seule.
- Mot de passe optionnel sur `/update` (aujourd'hui ouvert à tout le réseau local, comme l'origine).
- Découper `main.cpp` en modules quand il grossit.
