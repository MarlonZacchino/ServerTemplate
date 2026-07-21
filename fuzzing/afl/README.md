# AFL++ für den Styles-4-Dogs-Server

Der Server besitzt einen `stdin`-Modus. AFL++ übergibt deshalb jeden Testfall
direkt an:

```text
Server stdin
```

Es wird kein Netzwerk-Socket benötigt. `runtime_env.sh` setzt für Build-Prüfung,
Fuzzing und Crash-Replay dieselben isolierten Laufzeitvariablen. Für jeden
AFL-Prozess wird eine SQLite-Datenbank im Arbeitsspeicher verwendet.
Dadurch schreibt ein langer Fuzzing-Lauf weder echte Buchungen noch wachsende
Testdatenbanken auf die Festplatte. Das Startkorpus enthält außerdem einen
strukturell gültigen Admin-Statusrequest und eine Admin-Filteranfrage mit ungültigen Zugangsdaten.

## Abhängigkeiten unter Arch Linux

```bash
sudo pacman -S --needed afl++ clang cmake ninja libsodium sqlite curl pkgconf
```

## Instrumentierten Server bauen

```bash
./fuzzing/afl/build.sh
```

Optionaler AFL-Build mit ASan und UBSan:

```bash
./fuzzing/afl/build.sh asan
AFL_BUILD_DIR="$PWD/cmake-build-afl-asan" ./fuzzing/afl/run.sh
```

Der normale, schnellere AFL-Build sollte für längere Kampagnen bevorzugt werden.
Funde lassen sich anschließend gezielt mit Sanitizern wiederholen.

## Einzelne AFL-Instanz

```bash
./fuzzing/afl/run.sh
```

Ein vorhandener Lauf wird automatisch fortgesetzt. Für einen vollständig neuen
Lauf zuerst:

```bash
./fuzzing/afl/clean.sh
```

## Mehrere Instanzen

Standardmäßig wird ungefähr die Hälfte der CPU-Threads verwendet:

```bash
./fuzzing/afl/run_parallel.sh
```

Die Anzahl kann explizit gesetzt werden:

```bash
AFL_INSTANCES=4 ./fuzzing/afl/run_parallel.sh
```

Status in einem zweiten Terminal:

```bash
./fuzzing/afl/status.sh
```

## Crash mit ASan und UBSan wiederholen

```bash
./fuzzing/afl/replay_crash.sh \
  fuzzing/afl/out/default/crashes/id:000000,...
```

Bei parallelen Läufen liegen Funde unter `fuzzing/afl/sync/<Instanz>/crashes/`.

## Hinweise

- `fuzzing/afl/in/` und `http.dict` gehören ins Git.
- `out/`, `sync/` und `logs/` gehören nicht ins Git.
- AFL++ niemals gegen echte Buchungs- oder Secret-Dateien konfigurieren.
- Ein reproduzierbarer Crash wird erst mit ASan/UBSan analysiert und danach als
  fester Regressionstest ergänzt.

## Proxy- und Rate-Limit-Eingaben

Die AFL-Laufzeit setzt ein isoliertes Test-Proxy-Token. Das Korpus enthält einen
gültigen proxied Buchungsrequest, damit Header-Parsing und Client-IP-Auflösung
im instrumentierten Code erreichbar sind. Da jede AFL-Ausführung im
`stdin`-Modus genau einen Prozesslauf verarbeitet, wird der zustandsbehaftete
Limiter selbst durch PewPewLaz0rTank regressionsgetestet.

## Eigenes Kalender-Fuzzing-Ziel

Die Verfügbarkeitsengine ist noch nicht öffentlich geroutet. Damit ihre
Datums-, Service- und Slotlogik trotzdem direkt durch AFL++ erreichbar ist,
gibt es das separate Ziel `calendar_fuzz_target`.

```bash
cd fuzzing/afl
make calendar-build
make calendar-run
```

Für einen neuen Lauf:

```bash
make calendar-fresh
```

Das Eingabeformat ist ein kleiner URL-encoded Datensatz mit `service`, `date`,
`current_date`, `current_minute` und `now_utc`. Die Datenbank liegt weiterhin
nur im Arbeitsspeicher. Funde werden getrennt unter `calendar_out/` abgelegt.


## Öffentliche Kalender-Routen

Das HTTP-Korpus enthält Requests für `/api/services`, `/api/availability`,
das Buchungsformular mit `appointment_date` und `appointment_start` sowie die
geschützten Admin-Kalenderrouten mit ungültigen Zugangsdaten. Die Kalender-Engine
selbst bleibt zusätzlich über das separate `calendar_*`-Ziel instrumentiert.

## Phase-4-Korpus

Das HTTP-Korpus enthält zusätzlich strukturierte E-Mail- und WhatsApp-Kontakte
sowie ungültig authentifizierte Requests für Terminannahme und das Anlegen
einer Leistung. Dadurch werden die neuen Parser- und Adminrouten vom normalen
Server-Fuzzing erreicht.


## Phase-5-Korpus

Die Tages- und Wochenansicht unter `/admin/appointments` ist als weiterer
geschützter Request im HTTP-Korpus enthalten. Der SMTP-Worker wird nicht mit
Netzwerk-Fuzzing vermischt; Warteschlangen-, Erinnerungs- und ICS-Logik werden
über die isolierten Regressionstests und den Worker-Dry-Run geprüft.

## Phase-7-Korpus

Ein zusätzlicher Buchungsrequest füllt das unsichtbare Honeypot-Feld. Damit
wird die neutrale Bot-Antwort samt früher Request-Verarbeitung im normalen
HTTP-Fuzzing erreicht. Das zustandsabhängige Kontaktlimit bleibt Bestandteil
der isolierten PewPew-Regressionsprüfung.
