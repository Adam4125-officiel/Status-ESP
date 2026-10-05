# SmallTV-Ultra — firmware libre

Firmware alternatif pour l'écran connecté **GeekMagic SmallTV-Ultra** (ESP8266, écran
ST7789 240×240). Il s'installe **par le Wi-Fi, depuis la page de mise à jour de l'écran**,
sans ouvrir l'appareil ni brancher de câble, et permet à tout moment de **revenir au
firmware d'origine** par le même chemin.

> État : base de départ (écran, Wi-Fi, interface web, mise à jour). Les fonctions
> d'affichage restent à écrire.

## Installation
⚠️ **Uniquement pour le SmallTV-Ultra** (ESP8266). Pas pour SmallTV, Pro, HelloCubic… :
brochage et mémoire différents.

1. Télécharger le dernier zip dans les [Releases](../../releases) et le décompresser.
2. Vérifier le MD5 du `.bin` avec `md5sum.txt`
   (Windows : `Get-FileHash <fichier>.bin -Algorithm MD5`).
3. Ouvrir `http://<ip-de-l-ecran>/update` dans un navigateur (l'IP est dans les réglages
   du firmware d'origine), choisir le `.bin`, valider. Ne pas couper le courant.
4. L'écran redémarre et affiche l'adresse de sa nouvelle interface.

Le Wi-Fi est repris automatiquement du firmware d'origine. Les images et réglages
d'origine restent intacts.

## Fonctionnalités
- Affichage : titre, version, mire de couleurs, état du Wi-Fi et adresse de l'interface.
- En cas d'échec Wi-Fi : point d'accès **SmallTV-Custom** → `http://192.168.4.1`
  (choix du réseau, mise à jour), avec nouvel essai automatique toutes les 5 minutes.
- Interface web : état (mémoire, tailles, retour possible à l'origine), luminosité,
  polarité du rétroéclairage, changement de réseau, redémarrage.
- Mise à jour par `/update` (firmware uniquement : le formulaire qui effacerait les fichiers est masqué).

## Revenir au firmware d'origine
Envoyer le `.bin` officiel GeekMagic sur `/update`. Voir [docs/recovery.md](docs/recovery.md).

## Compiler soi-même
Prérequis : Python 3.10+.

| | Windows (PowerShell) | Linux |
|---|---|---|
| Installer PlatformIO (dans `.venv`, local au projet) | `.\tools\setup.ps1` | `bash tools/setup.sh` |
| Compiler + vérifier la taille | `.\tools\build.ps1` | `bash tools/build.sh` |
| Fabriquer le zip de release | `py tools\make_release.py` | `python3 tools/make_release.py` |
| Envoyer sur l'écran | `.\tools\upload.ps1 -Ip <ip>` | `curl -F "firmware=@.pio/build/smalltv-ultra/firmware.bin" http://<ip>/update` |

Sous Windows, placer le projet dans un dossier à **chemin court**
(ex. `C:\Users\<nom>\SmallTV-Ultra-Firmware`) : le compilateur ne gère pas les chemins longs.

Les Releases sont aussi produites automatiquement par GitHub Actions quand un tag
`vX.Y.Z` (égal à la version de `platformio.ini`) est poussé.

## Documentation
- [docs/hardware.md](docs/hardware.md) : composants, brochage, découpage de la flash, contrainte de taille
- [docs/stock-firmware.md](docs/stock-firmware.md) : analyse du firmware d'origine (API, fichiers)
- [docs/recovery.md](docs/recovery.md) : retour à l'origine et dépannage
- [CLAUDE.md](CLAUDE.md) : guide pour les agents IA qui travaillent sur le projet
- [CHANGELOG.md](CHANGELOG.md)

## Avertissement
Projet non officiel, sans lien avec GeekMagic. Modifier le firmware se fait à vos risques.
Les firmwares GeekMagic ne sont pas redistribués ici.

## Bibliothèques
- [ESP8266 Arduino core](https://github.com/esp8266/Arduino) 3.1.2 (via PlatformIO espressif8266 4.2.1)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) 2.5.43
