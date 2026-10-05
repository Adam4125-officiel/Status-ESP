# Retour au firmware d'origine et dépannage

Tout se fait par le Wi-Fi, depuis un navigateur.

## Revenir au firmware GeekMagic
1. Télécharger le firmware officiel sur le
   [dépôt GeekMagic](https://github.com/GeekMagicClock/smalltv-ultra) (dossier `Ultra-V…`),
   décompresser le zip et vérifier le MD5 fourni.
2. Ouvrir `http://<ip-de-l-ecran>/update` (l'IP s'affiche sur l'écran au démarrage).
3. Envoyer le `.bin` GeekMagic. L'écran redémarre sur le firmware d'origine.

Les images, réglages et identifiants Wi-Fi d'origine sont intacts : ce firmware ne
formate jamais la zone de fichiers.

Pour contrôler un `.bin` avant envoi : `python tools/check_firmware.py <fichier.bin>`.

## L'écran affiche « Pas de Wi-Fi »
Le firmware ouvre alors le réseau **SmallTV-Custom** (sans mot de passe) :
- `http://192.168.4.1/wifi` : choisir un nouveau réseau Wi-Fi ;
- `http://192.168.4.1/update` : installer un autre firmware ou revenir à l'origine.

Sans personne connecté dessus pendant 5 minutes, l'écran redémarre et réessaie le Wi-Fi
(utile après une coupure de courant, quand la box redémarre plus lentement).

## La mise à jour est refusée
- « Not Enough Space » : le `.bin` est trop gros pour l'espace libre (voir
  [hardware.md](hardware.md#mise-à-jour-par-le-wi-fi--contrainte-de-taille)).
- « Magic Byte » : le fichier n'est pas un firmware ESP8266 (zip non décompressé,
  fichier corrompu ou modifié par un antivirus) : vérifier le MD5.
