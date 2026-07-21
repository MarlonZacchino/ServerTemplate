# Phase 8 – Branding und Fotogalerie

Phase 8 ersetzt die sichtbare Kurzmarke `S4D` durch das echte Logo und verwendet
auf allen öffentlichen und administrativen Oberflächen den Namen
**Styling 4 Dogs**.

## Öffentliche Galerie

Neue Routen:

```text
/galerie
/api/gallery
/media/<dateiname>
```

`/galerie` lädt ausschließlich freigegebene Bilder. Die JSON-API liefert Titel,
Alternativtext und die öffentliche Bild-URL. Ausgeblendete Bilder werden weder
in der API aufgelistet noch über `/media/` ausgeliefert.

## Adminbereich

Neue geschützte Route:

```text
/admin/gallery
```

Der Admin kann dort:

- JPG-, PNG- und WebP-Bilder bis 8 MB hochladen,
- Titel und Alternativtext festlegen,
- eine Sortierreihenfolge angeben,
- Bilder direkt sichtbar oder zunächst ausgeblendet speichern,
- Bilder wieder löschen.

Vor dem Upload muss bestätigt werden, dass die Veröffentlichung abgestimmt ist.
Der Adminbereich erinnert zusätzlich daran, Fotos ohne Standort- oder andere
unnötige Metadaten zu exportieren.

## Speicherung und Backup

Bilddaten und Metadaten liegen gemeinsam in der SQLite-Tabelle
`gallery_images`. Dadurch enthalten die bestehenden SQLite-Backups automatisch
auch die Galerie. Das Schema wird idempotent auf Version 7 migriert:

```text
PRAGMA user_version = 7
```

## Upload-Schutz

- Admin Basic Auth
- CSRF-Token auch bei `multipart/form-data`
- 8-MB-Anwendungslimit
- 9-MiB-Caddy-Limit einschließlich Multipart-Overhead
- serverseitige Erkennung der tatsächlichen JPEG-, PNG- oder WebP-Signatur
- `nosniff` bei Bildantworten
- keine Auslieferung ausgeblendeter Bilder über die öffentliche Route
