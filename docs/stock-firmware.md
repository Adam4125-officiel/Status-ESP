# Firmware d'origine GeekMagic (Ultra-V9.0.50 / 9.0.51) — notes d'analyse

Analyse statique avec `tools/analyze_firmware.py`. Le code source n'est pas public :
on n'a que le binaire compilé. Le firmware lui-même **n'est pas redistribué ici**
(propriétaire) ; la 9.0.50 est sur le [dépôt officiel](https://github.com/GeekMagicClock/smalltv-ultra).

## Généralités
- Arduino ESP8266 (SDK NONOS compilé en 2019), LittleFS, image de 505 200 octets.
- Identification : `GET /v.json` → `{"m": "SmallTV-Ultra","v":"Ultra-V9.0.51"}`.
- La 9.0.51 (version d'usine) n'est publiée nulle part ; elle diffère réellement de la
  9.0.50 (~351 000 octets différents) malgré une taille identique.
- Interface web : 4 pages HTML/JS stockées compressées (gzip) dans le firmware :
  `time.html`, `weather.html`, `settings.html`, `image.html`.

## API HTTP (port 80, sans authentification)
| Route | Rôle |
|---|---|
| `GET /set?<param>=<valeur>` | Modifie un réglage (voir liste ci-dessous) |
| `GET /<nom>.json` | Lit un réglage |
| `POST /doUpload?dir=<dossier>` | Envoie un fichier (image/GIF) dans LittleFS |
| `GET /delete?file=<chemin>` | Supprime un fichier |
| `GET /filelist?dir=<dossier>` | Liste un dossier (HTML) |
| `GET /space.json` | `{"total":…,"free":…}` de LittleFS |
| `GET/POST /update` | Mise à jour firmware (`ESP8266HTTPUpdateServer`, champ `firmware`) ; formulaire « filesystem » masqué en commentaire HTML |
| `/wifisave`, `/generate_204`, `/hotspot-detect.html`, `/fwlink` | Portail captif de configuration Wi-Fi |

Paramètres `/set` repérés : `brt`, `theme`, `theme_list`, `sw_en`, `theme_interval`,
`tz_auto`, `tz_offset`, `hour`, `font`, `colon`, `ntp`, `day`, `hc`/`mc`/`sc` (couleurs
heure/minute/seconde), `time_interval`, `yr`/`mth`/`day`, `key`, `fkey`,
`w_u`/`t_u`/`p_u` (unités), `cd1`/`cd2`, `w_i`, `gif`, `img`, `i_i`, `autoplay`,
`clear=gif|image`, `t1`/`t2`/`b1`/`b2`/`en` (mode nuit), `reboot=1`, `reset=1`.

Fichiers JSON de réglages (dans LittleFS) : `/config.json` (Wi-Fi : `{"a":"<ssid>","p":"<mdp>"}`,
mot de passe masqué par l'API), `/city.json`, `/key.json`, `/fkey.json`, `/unit.json`,
`/ntp.json`, `/tz.json`, `/dst.json`, `/brt.json`, `/timebrt.json`, `/delay.json`,
`/font.json`, `/gif.json`, `/img.json`, `/album.json`, `/app.json`, `/theme_list.json`,
`/hour12.json`, `/rotation.json`, `/colon.json`, `/day.json`, `/timecolor.json`,
`/lon.json`, `/w_i.json`, `/space.json`, `/v.json`, `/wifi.json`.

Autres fichiers utilisés : `/image/…` (photos), `/gif/…` (animations),
`/image/boot.jpg|gif`, `/Alibaba20.vlw` (police lissée).

## Services externes contactés
- Météo : `api.openweathermap.org` (clé API utilisateur via `/set?key=`) et `api.open-meteo.com`.
- Heure : NTP, par défaut `ntp.aliyun.com`.
- Le binaire contient aussi 9 chaînes hexadécimales de 32 caractères, peut-être des clés API
  par défaut ou des empreintes. Volontairement **non reproduites** ici.

## Points notables
- `/update` sans mot de passe : toute machine du réseau local peut reflasher l'appareil.
- Notre firmware relit `/config.json` pour réutiliser les identifiants Wi-Fi d'origine.
