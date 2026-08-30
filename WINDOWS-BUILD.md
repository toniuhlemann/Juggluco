# Juggluco auf Windows bauen (Stand 04.08.2026, Basis 10.9.8)

Jaap baut auf Linux; auf Windows sind einmalig diese Schritte noetig. Ergebnis-Variante:
`:Common:assembleMobileLibre3SiDexNogoogleDebug` (Handy, Libre 2/3 + Sibionics + Dexcom,
Debug = eigene App-ID `tk.glucodata.debug`, laeuft NEBEN dem offiziellen Juggluco).

## Toolchain
- JDK: Android Studio JBR (`JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`)
- NDK **30.0.14904198** (Pin in `Common/build.gradle`, NICHT gradle.properties — die wird
  von der lokalen `def ndkver` ueberschattet)
- CMake **4.1.2** (Pin `CMAKEVERSION` in `Common/build.gradle:38`), beides per SDK Manager
- `local.properties`: NUR `sdk.dir=C:/Users/<user>/AppData/Local/Android/Sdk`
  (Vorwaertsschraegstriche! Jaaps eingecheckte Version traegt seine Linux-Pfade;
  lokal ueberschreiben, per `git update-index --skip-worktree local.properties` stillgelegt)

## Einmalige Repo-Reparaturen (nicht committet, nach frischem Clone wiederholen)
1. **Symlinks**: das Repo enthaelt ~73 Git-Symlinks (mode 120000), die Windows ohne
   Developer Mode als Textdateien auscheckt -> Build bricht mit "file name must end
   with .xml" o.ae. Fix: jede Symlink-Datei durch Kopie ihres Ziels ersetzen:
   ```bash
   git ls-files -s | awk '$1=="120000"{print $4}' > /tmp/links.txt
   while read -r f; do t=$(cat "$f"); d=$(dirname "$f");
     [ -e "$d/$t" ] && rm "$f" && cp -r "$d/$t" "$f"; done < /tmp/links.txt
   ```
   (8 Ziele fehlen — alle nur fuer wear/small-Flavors, fuer mobile egal.)
2. **Submodul**: `git submodule update --init --recursive` (libjuice).

## Committete Windows-Anpassung (dieser Branch)
`Common/src/main/cpp/CMakeLists.txt` + `cmake/skip_rtlpp.cmake`: das rtlpp-Hostwerkzeug
(braucht Host-Ninja/Compiler/ICU) wird auf Windows-Hosts uebersprungen; die arabische
Textdatei wird stattdessen mit Identitaets-Makros (`RTL`/`RTLFMT`) und `u8`/`u8R`-
Prefix-Strip durchgereicht. Einzige Folge: arabische UI-Labels rendern in Lese- statt
Darstellungsreihenfolge. Alle anderen Sprachen unberuehrt.

## Eigener Release-Kanal (stabile Signatur, seit 30.08.2026)
Upstream signiert ALLE Buildtypen (auch debug) mit `signingConfigs.alg`; ohne eigene
Konfiguration faellt das auf den eingecheckten `everyone.keystore` mit oeffentlich
bekanntem Passwort zurueck — fuer den Produktivkanal ungeeignet (jeder koennte ein
"Update" bauen). Der Upstream-Mechanismus liest stattdessen vier Properties aus
`~/.gradle/gradle.properties`:
```
thepassword = <secret>
thekeyalias = jugglucotoni
thekeypassword = <secret>
thekeyfile = C:/Users/toniu/keystores/juggluco-toni.jks
```
- Keystore: `C:\Users\toniu\keystores\juggluco-toni.jks` (RSA 4096, Alias `jugglucotoni`,
  Zert-SHA256 `EE:50:82:A0:AD:B8:A3:71:87:CF:CC:B4:3F:AA:74:53:BE:93:CB:31:BC:8B:32:EE:9F:F8:B8:E8:DF:3A:D4:05`).
- **BACKUP-PFLICHT: Keystore-Datei UND `~/.gradle/gradle.properties` extern sichern.**
  Ohne den Schluessel ist kein In-place-Update der installierten App mehr moeglich
  (Neuinstallation = Bonding-/Datenverlust). Keystore und Passwoerter gehoeren NIE ins Repo.
- Der debug-Buildtype traegt auf diesem Branch das Versionssuffix `DEBUG-toni+<commit8>`
  (+ `BuildConfig.GIT_COMMIT`): App-Info am Geraet zeigt damit den exakten Quellstand.
- Signatur einer APK pruefen: `build-tools/36.0.0/apksigner verify --print-certs <apk>`.

### Rollback-Build
Beide APKs (Produktions-Stand und Rollback-Stand `l3-diagnostics-toni`) haben denselben
versionCode (901) und ab jetzt dieselbe Signatur -> am Geraet in BEIDE Richtungen per
`adb install -r` wechselbar, ohne Deinstallation. Rollback bauen:
```
git checkout --detach <l3-diagnostics-toni-Commit>
JAVA_HOME=... ./gradlew :Common:assembleMobileLibre3SiDexNogoogleDebug
git checkout primary-sensor-routing-toni
```
(Der Rollback-Stand hat kein toni-Versionssuffix — daran am Geraet erkennbar.)
Gebaute Paare liegen mit Manifest unter `C:\Users\toniu\JugglucoBuilds\`.

## Bekannte Eigenheiten
- Release-Builds definieren `NOLOG=1` + `NORAWSTREAM=1`; Debug-Builds loggen (`SCANLOG`)
  und schreiben den rohen BLE-Stream in die Sensor-Verzeichnisse (`rawstream`-Datei).
- Flavor-Kombinationen sind gefiltert: libre3 existiert nur zusammen mit si+dex.

## Wechseltag-Checkliste (offiziell -> DIAG, am Sensorwechsel)
1. Neuen Sensor in **Juggluco DIAG** aktivieren (App-ID tk.glucodata.debug, laeuft parallel).
2. In DIAG konfigurieren (startet mit frischen Einstellungen!): xDrip-Broadcast an AAPS AN,
   Webserver AN (Port 17580), ggf. Mirror zum Testhandy, Einheiten/Alarme.
3. In der OFFIZIELLEN App: xDrip-Broadcast AUS und Webserver AUS — oder die App komplett
   deaktivieren (narrensicher, umkehrbar). Es darf nur EINE App an AAPS senden und nur
   EINE den Port 17580 halten, sonst liest der Viewer die falsche (tote) Quelle.
4. Gegenprobe: AAPS zeigt frische Werte; Viewer-JG == Loop-BG.
5. Diagnose-Datei bei Bedarf: adb exec-out run-as tk.glucodata.debug sh -c
   'cat files/<sensordir>/l3diag.csv'
