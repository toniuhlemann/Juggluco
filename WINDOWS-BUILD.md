# Juggluco auf Windows bauen (Stand 14.09.2026, Basis 11.0.2 = upstream 23ebeaa8)

Jaap baut auf Linux; auf Windows sind einmalig diese Schritte noetig. Ergebnis-Variante:
`:Common:assembleMobileLibre3SiDexNogoogleDebug` (Handy, Libre 2/3 + Sibionics + Dexcom,
Debug = eigene App-ID `tk.glucodata.debug`, laeuft NEBEN dem offiziellen Juggluco).

## Toolchain
- JDK: Android Studio JBR (`JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`)
- NDK: Pin `ndkver` in `Common/build.gradle` (NICHT gradle.properties — die wird von der
  lokalen `def ndkver` ueberschattet). Stand 11.0.2: **30.0.16138531**. Fehlt das Pin-NDK,
  installiert AGP es beim ersten Build selbst (SDK-Lizenz muss akzeptiert sein).
- CMake **4.1.2** (Pin `CMAKEVERSION` in `Common/build.gradle`), per SDK Manager
- `local.properties`: NUR `sdk.dir=C:/Users/<user>/AppData/Local/Android/Sdk`
  (Vorwaertsschraegstriche! Jaaps eingecheckte Version traegt seine Linux-Pfade;
  lokal ueberschreiben, per `git update-index --skip-worktree local.properties` stillgelegt)

## Einmalige Repo-Reparaturen (nicht committet, nach frischem Clone wiederholen)
1. **Symlinks**: das Repo enthaelt ~75 Git-Symlinks (mode 120000), die Windows ohne
   Developer Mode als Textdateien auscheckt -> Build bricht mit "file name must end
   with .xml" o.ae. Fix: jede Symlink-Datei durch Kopie ihres Ziels ersetzen:
   ```bash
   git ls-files -s | awk '$1=="120000"{print $4}' > /tmp/links.txt
   while read -r f; do t=$(cat "$f"); d=$(dirname "$f");
     [ -e "$d/$t" ] && rm "$f" && cp -r "$d/$t" "$f"; done < /tmp/links.txt
   ```
   (Einige Ziele fehlen — alle nur fuer wear/small-Flavors, fuer mobile egal.)
2. **Submodul**: `git submodule update --init --recursive` (libjuice).

## Committete Windows-Anpassung (dieser Branch)
`Common/src/main/cpp/CMakeLists.txt` + `cmake/skip_rtlpp.cmake`: das rtlpp-Hostwerkzeug
(braucht Host-Ninja/Compiler/ICU) wird auf Windows-Hosts uebersprungen; die arabische
Textdatei wird stattdessen mit Identitaets-Makros (`RTL`/`RTLFMT`) und `u8`/`u8R`-
Prefix-Strip durchgereicht. Einzige Folge: arabische UI-Labels rendern in Lese- statt
Darstellungsreihenfolge. Alle anderen Sprachen unberuehrt.

Der Build schreibt dabei `Common/src/main/cpp/curve/arjugglucotextgen.cpp` (eingecheckte,
upstream generierte Datei) lokal neu; sie steht deshalb auf skip-worktree. Aendert upstream
diese Datei, blockiert sie einen Merge: vorher
`git update-index --no-skip-worktree <datei>` + `git checkout -- <datei>`, nach dem Merge-
Commit wieder `git update-index --skip-worktree <datei>`.

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
- Der debug-Buildtype traegt das Versionssuffix `DEBUG-toni+<commit8>`
  (+ `BuildConfig.GIT_COMMIT`): App-Info am Geraet zeigt damit den exakten Quellstand.
- APK pruefen: `build-tools/36.0.0/apksigner verify --print-certs <apk>` (Signatur),
  `build-tools/36.0.0/aapt2 dump badging <apk>` (Paket, versionCode, versionName).

### Rueckwechsel (Rollback)
Gleiche Signatur ist NOTWENDIG, aber nicht hinreichend. Der versionCode kommt aus der
Upstream-Basis: 10.10.0-Staende = 901, 11.0.2-Staende = 907.
- Vorwaerts (901 -> 907): `adb install -r <apk>`.
- Rueckwaerts (907 -> 901): `adb install -r -d <apk>`. Das ist ein Downgrade; Android
  erlaubt ihn nur, weil die Debug-Builds debuggable sind. Ohne `-d` wird abgelehnt.

Datenkompatibilitaet (am Code geprueft, 14.09.2026):
- `settings.dat` wird von keinem Stand gekuerzt: Mmap vergroessert nur, der Mirror-Empfang
  schreibt per `pwrite` an Offsets. Aeltere Staende ignorieren Bytes hinter ihrem
  `Tings`-Ende; die Primaer-Epochen ueberleben einen Rueckwechsel und sind beim erneuten
  Vorwaertswechsel unveraendert da.
- `Tings`-Layout der 11.0.2-Integration ist byteidentisch zu 738f12aa (Offset-Probe per
  NDK-Clang, arm64 + arm32); alle gemeinsamen Felder liegen wie in 10.10.0.
- Die Mirror-Settings-Synchronisation (`datbackup.cpp`) kopiert nur benannte Teilbereiche,
  nie den Epochen-Block: jedes Geraet fuehrt seine Epochen selbst.
- 11.0.2 speichert den BLE-Mirror-Transport in bisher reservierten Bits von `passhost_t`;
  10.10.0 ignoriert sie -> nach einem Rueckwechsel laeuft ein Bluetooth-Mirror nicht,
  ein TCP-Mirror weiter. Die neuen Rotations-Bits in `Tings` ignoriert 10.10.0 ebenfalls.

Rollback-Stand bauen, ohne einen Branch zu veraendern:
```
git checkout --detach <commit>
JAVA_HOME=... ./gradlew :Common:assembleMobileLibre3SiDexNogoogleDebug
git checkout <arbeitsbranch>
```
Staende ohne toni-Suffix sind Rollback-Staende ohne Routing und daran am Geraet erkennbar.
Gebaute APKs liegen mit MANIFEST.md unter `C:\Users\toniu\JugglucoBuilds\`.

## Upstream-Sync
- `primary` ist ein unveraenderter Spiegel von j-kaltes/Juggluco. Integriert wird auf einem
  eigenen Arbeitsbranch (z.B. `integration-11.0.2-toni`); Feature-Branch und alte APKs
  bleiben erhalten.
- Versionsnummer nie kosmetisch hochsetzen: der Build traegt die Version seines
  tatsaechlichen Quellstands. (11.1.0 war am 14.09.2026 nur als APK veroeffentlicht,
  nicht als Quelle; GitHub `primary` stand auf 11.0.2, der Tag `Newest` ist alt.)
- Nach jedem Merge pruefen: `Tings`-Layout (Offset-Probe), Mirror-Sync-Bereiche in
  `datbackup.cpp` (duerfen den Epochen-Block nie einschliessen), neue Glukose-Ausgaenge
  im Upstream-Diff, Grid-Umbau in `bluediag.setPhoneSensorLayout` (Primaersensor-Zeile
  muss in beiden Modi eingetragen sein).
- App-Label: `aapt2 dump badging <apk> | grep application-label` muss in JEDER Sprache
  `Juggluco DIAG` zeigen. Definiert upstream `app_name` in einer neuen `values-xx`-Datei,
  braucht `Common/src/debug/res/values-xx/strings.xml` eine eigene Ueberschreibung, sonst
  heisst die Debug-App auf Geraeten in dieser Sprache wieder "Juggluco".

## Bekannte Eigenheiten
- Release-Builds definieren `NOLOG=1` + `NORAWSTREAM=1`; Debug-Builds loggen (`SCANLOG`)
  und schreiben den rohen BLE-Stream in die Sensor-Verzeichnisse (`rawstream`-Datei).
- Flavor-Kombinationen sind gefiltert: libre3 existiert nur zusammen mit si+dex.
- Ab 11.0.0 baut upstream nur noch arm64-v8a und armeabi-v7a; x86/x86_64 entfallen.

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
