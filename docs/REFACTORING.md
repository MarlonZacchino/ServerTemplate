# Struktur-Refactoring

Dieses Refactoring ordnet den bestehenden Code nach fachlichen Modulen, ohne
HTTP-Routen, Datenbankschema, Laufzeitpfade oder Binärnamen zu ändern.

## Wichtigste Änderungen

- C-Implementierungen liegen unter `src/` und sind nach Verantwortlichkeit
  gruppiert.
- Öffentliche Header liegen unter `include/styles4dogs/` und werden mit ihrem
  vollständigen Modulpfad eingebunden.
- Historische Phasendokumente liegen unter `docs/history/`.
- Betriebsdokumentation liegt unter `docs/operations/`.
- Der gemeinsame Anwendungscode wird einmal als statische Bibliothek
  `styles4dogs_core` kompiliert. Server, Worker, Tests und Fuzzing-Ziele
  verlinken diese Bibliothek.
- Alle öffentlichen Funktionsdeklarationen besitzen Doxygen-Kommentare.
- Bei installiertem Doxygen steht das CMake-Ziel `documentation` bereit.

## Kompatibilität

Folgende Namen und Schnittstellen bleiben erhalten:

- Binärdatei `Server`
- Binärdatei `notification_worker`
- CMake-Ziele für Server, Worker, Tests und Fuzzing
- öffentliche HTTP-Routen
- Environment-Variablen
- Produktionspfade unter `/opt`, `/etc`, `/var/lib` und `/var/www`
- installierte Dokumentnamen unter `/opt/styles4dogs/share`

## Neue Include-Schreibweise

Vorher:

```c
#include "booking_database.h"
#include "http_lib.h"
```

Nachher:

```c
#include "styles4dogs/booking/booking_database.h"
#include "styles4dogs/http/http_lib.h"
```

Dadurch ist bereits am Include erkennbar, zu welchem Modul eine Schnittstelle
gehört, und gleichnamige Header können später ohne Konflikte ergänzt werden.
