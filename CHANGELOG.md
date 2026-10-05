# Changelog

## 0.2.0 — non encore installée sur l'appareil
- Suppression du code « bouton » : le SmallTV-Ultra n'a ni bouton ni zone tactile.
- Page `/update` propre : uniquement le firmware (le formulaire « FileSystem », qui
  effacerait toute la zone de fichiers, n'est plus proposé).
- Texte de retour à l'origine mis à jour.
- Versions figées : espressif8266 4.2.1 (core Arduino 3.1.2), TFT_eSPI 2.5.43.
- Outils : scripts Windows et Linux (setup, build), envoi par `/update`, fabrication
  du zip de release, vérification CRC d'un `.bin`, analyse du firmware d'origine.
- GitHub Actions : compilation à chaque push, Release automatique sur tag `vX.Y.Z`.

## 0.1.0 — installée et validée sur l'appareil
- Écran ST7789 (SPI mode 3) : couleurs et rétroéclairage (GPIO5 inversé) validés.
- Wi-Fi : identifiants du SDK, puis `/config.json` d'origine, puis point d'accès de secours.
- Interface web : état, luminosité, polarité du rétroéclairage, choix du Wi-Fi, redémarrage.
- `/update` (ESP8266HTTPUpdateServer), LittleFS monté sans formatage automatique.
