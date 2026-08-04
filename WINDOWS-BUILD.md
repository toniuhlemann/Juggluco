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

## Bekannte Eigenheiten
- Release-Builds definieren `NOLOG=1` + `NORAWSTREAM=1`; Debug-Builds loggen (`SCANLOG`)
  und schreiben den rohen BLE-Stream in die Sensor-Verzeichnisse (`rawstream`-Datei).
- Flavor-Kombinationen sind gefiltert: libre3 existiert nur zusammen mit si+dex.
