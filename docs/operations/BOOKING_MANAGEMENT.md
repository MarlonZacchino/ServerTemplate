# Buchungsverwaltung, Verlauf und Admin-Sitzungen

Seit Schema-Version 14 deckt die Anwendung den vollständigen Lebenszyklus eines
Termins ab: Anfrage, Bestätigung, Bearbeitung, Verschiebung, Erinnerung,
Stornierung, Erledigung und „Nicht erschienen“.

## Admin-Anmeldung

Der eigentliche Adminzugang erfolgt über:

```text
/admin/login
```

Vorhandene Zugangsdaten aus `admin.auth` werden beim Start in `admin_users`
übernommen. Passwörter bleiben als libsodium-Argon2-Hash gespeichert. Nach der
Anmeldung wird eine serverseitige Sitzung erzeugt. In der Datenbank liegt nur
der Hash des zufälligen Sitzungstokens; das rohe Token wird ausschließlich im
Browsercookie übertragen.

Das Cookie verwendet `HttpOnly`, `SameSite=Lax`, `Path=/admin` und eine
begrenzte Lebensdauer. Bei einer öffentlichen HTTPS-Basis-URL wird zusätzlich
`Secure` gesetzt. Sitzungen laufen nach 30 Minuten Inaktivität oder spätestens
nach zwölf Stunden ab. Eine neue Anmeldung rotiert die bisherige Sitzung.
Logout und alle schreibenden Adminformulare sind CSRF-geschützt.

HTTP Basic Auth bleibt vorübergehend als Migrations- und Notfallzugang
kompatibel. Die normale Bedienung soll über die Loginseite erfolgen.

## Buchungen bearbeiten und verschieben

Auf jeder Buchungskarte führt **Bearbeiten** zu:

```text
/admin/bookings/edit?id=BUCHUNGSNUMMER
```

Dort können Kontakt-, Adress-, Hunde- und Termindaten sowie interne Notizen
bearbeitet werden. Die Hunderasse ist optional.

Eine Änderung von Datum, Startzeit oder Leistung gilt als Terminverschiebung.
Vor dem Speichern prüft das Backend innerhalb einer SQLite-Transaktion:

- aktive Leistung, Dauer und Puffer,
- Öffnungszeiten und Schließzeiten,
- Mindestvorlauf und Buchungshorizont,
- das konfigurierte Zeitraster,
- Überschneidungen mit anderen Pending- oder bestätigten Buchungen.

Die bearbeitete Buchung wird bei der Kollisionsprüfung ausgeschlossen. Bei
einem Konflikt wird keine Teiländerung gespeichert. Eine erfolgreiche
Verschiebung erzeugt `booking_rescheduled` und eine aktualisierte ICS-Datei mit
derselben stabilen UID wie die ursprüngliche Bestätigung.

## Kundenabsage

Im persönlichen Kundenportal kann ein optionaler Absagegrund angegeben werden.
Die konfigurierte Frist steht in `calendar_settings.cancellation_notice_minutes`
und beträgt standardmäßig 4320 Minuten beziehungsweise 72 Stunden.

Gespeichert werden Stornierungszeitpunkt, Grund, Auslöser und die Kennzeichnung
einer kurzfristigen Absage. Nach dem Terminende sowie bei bereits abgelehnten,
abgesagten, erledigten oder als nicht erschienen markierten Buchungen ist keine
Onlineabsage mehr möglich.

Bei einer Kundenabsage werden genau einmal eingereiht:

- `booking_cancelled` an den Kunden,
- `admin_booking_cancelled` an die konfigurierte Adminadresse.

Die Anwendung dokumentiert eine kurzfristige Absage, berechnet aber keine
Gebühr und erstellt keine Rechnung.

## Kunden und Hunde

`customers` und `dogs` speichern die aktuelle Stammdatenansicht. Eine Buchung
behält zusätzlich ihre Snapshots, damit historische Termine unverändert
nachvollziehbar bleiben.

Ein Kunde wird nur anhand einer normalisierten E-Mail-Adresse oder – wenn keine
E-Mail vorliegt – einer normalisierten Telefonnummer wiedererkannt. Gleiche
Namen allein führen nicht zu einer Zusammenführung. Hunde werden nur innerhalb
eines eindeutig erkannten Kunden anhand ihres Namens zugeordnet.

Interne Admin- und Hundenotizen erscheinen weder im Kundenportal noch in
Kundenmails.

## Audit-Verlauf

`booking_events` speichert fachliche Ereignisse mit Akteur, Zeitpunkt,
vorherigem und neuem Wert sowie optionalem Grund. Pro Buchung zeigt der
Adminbereich einen chronologischen, HTML-escaped Verlauf. Geheimnisse wie
Passwörter, Sitzungstokens, CSRF-Tokens oder SMTP-Zugangsdaten werden nicht
protokolliert.

## Nicht erschienen

Nur ein Admin kann eine bestätigte Buchung auf **Nicht erschienen** setzen.
Der technische Status lautet `no_show`, der sichtbare Status
`nicht_erschienen`. Er erscheint in Filtern, Gruppen, Zählern und Historien,
erzeugt keine Kundenmail und wird von der automatischen Erledigung nicht
überschrieben.

## Manuelle Prüfung

1. Unter `/admin/login` anmelden und Logout testen.
2. Eine neue Buchung anlegen und im Adminbereich bestätigen.
3. Die Buchung bearbeiten und auf einen freien Termin verschieben.
4. Kundenmail und stabile ICS-UID prüfen.
5. Eine Kollision mit einem belegten Termin auslösen; die alte Buchung muss
   unverändert bleiben.
6. Im Kundenportal mit optionalem Grund absagen und beide E-Mail-Jobs prüfen.
7. Verlauf, Kundenhistorie und Hundehistorie öffnen.
8. Eine interne Hundenotiz speichern und kontrollieren, dass sie öffentlich
   nicht erscheint.
9. Eine bestätigte Buchung als „Nicht erschienen“ markieren.
