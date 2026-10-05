# Matériel — GeekMagic SmallTV-Ultra

Légende : ✅ vérifié sur l'appareil · ❓ inconnu

## Composants
| Élément | Détail | Statut |
|---|---|---|
| SoC | ESP8266EX, quartz 26 MHz, module type ESP-12F | ✅ |
| Flash | 4 Mo, mode DIO 40 MHz | ✅ |
| Écran | ST7789 240×240, SPI **mode 3**, 40 MHz, couleurs RVB correctes sans inversion | ✅ |
| Rétroéclairage | GPIO5, PWM **inversé** (duty bas = lumineux) | ✅ |
| Bouton / tactile | **Aucun** | ✅ |
| Alimentation | USB-C (alimentation seule, pas de port série USB) | ✅ |
| Capteurs, son | Aucun connu | ❓ |

L'appareil se pilote donc uniquement par le réseau.

## Brochage
| GPIO | Fonction | Remarque |
|---|---|---|
| 13 | SPI MOSI (écran) | |
| 14 | SPI SCLK (écran) | |
| 0 | DC écran | **Broche de démarrage** : ne pas la forcer à l'état bas au boot |
| 2 | RST écran | **Broche de démarrage** : doit être haute au boot |
| 5 | Rétroéclairage (PWM inversé) | |
| — | CS écran | Non câblé (relié à la masse) |
| 4, 12, 15, 16 | Libres ? | ❓ non vérifié, ne pas supposer |

## Découpage de la flash (identique au firmware d'origine, « 4M3M »)
| Adresse | Taille | Contenu |
|---|---|---|
| `0x000000` | 4 Ko | Bootloader Arduino `eboot` |
| `0x001000` | ~1 Mo | Application (firmware actif) + espace pour la mise à jour suivante |
| `0x100000` | `0x2FA000` (3 121 152 o) | LittleFS : fichiers (images, réglages `.json`, Wi-Fi d'origine…) |
| `0x3FA000` | 24 Ko | Zone système : EEPROM, calibration RF, config Wi-Fi du SDK |

`/space.json` du firmware d'origine renvoie `total: 3121152`, ce qui confirme ce découpage.

## Mise à jour par le Wi-Fi : contrainte de taille
La mise à jour Arduino écrit la nouvelle image **dans l'espace libre derrière
l'image actuelle**, avant la zone LittleFS, puis le bootloader la recopie.

| Firmware installé | Taille | Espace libre pour la mise à jour |
|---|---|---|
| GeekMagic 9.0.50 / 9.0.51 | 505 200 o | 540 672 o |
| Custom 0.2.0 | ~376 000 o | ~671 000 o |

D'où les deux règles du projet :
1. notre firmware doit rester **< ~520 Ko** pour pouvoir être installé depuis le firmware d'origine ;
2. il doit laisser **≥ 505 200 o** libres pour permettre le retour au firmware d'origine.
