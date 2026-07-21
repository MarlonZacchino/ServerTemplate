#!/usr/bin/env python3
"""HTTP regression tests for the Styling 4 Dogs C server."""

from __future__ import annotations

import base64
import json
import os
import re
import socket
import sqlite3
import sys
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo
from pathlib import Path
from urllib.parse import urlencode

from pewpewlaz0rt4nk import Beam, Laz0rCannon

HOST = sys.argv[1] if len(sys.argv) >= 2 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) >= 3 else 31337
TIMEOUT = 5
TEST_ADMIN_USERNAME = "test-admin"
TEST_ADMIN_PASSWORD = "Styles4Dogs-Test-2026!"
TEST_PROXY_TOKEN = os.environ.get(
    "STYLES4DOGS_TEST_PROXY_TOKEN",
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
)


def request_text(method: str, path: str, headers: dict[str, str] | None = None, body: str = "") -> str:
    all_headers = {"Host": f"{HOST}:{PORT}", **(headers or {})}
    if body and "Content-Length" not in all_headers:
        all_headers["Content-Length"] = str(len(body.encode("utf-8")))

    header_lines = "".join(f"{name}: {value}\r\n" for name, value in all_headers.items())
    return f"{method} {path} HTTP/1.1\r\n{header_lines}\r\n{body}"


def proxy_headers(client_ip: str, extra: dict[str, str] | None = None) -> dict[str, str]:
    return {
        "X-Forwarded-For": client_ip,
        "X-Styles4Dogs-Proxy-Token": TEST_PROXY_TOKEN,
        **(extra or {}),
    }


def email_contact(address: str) -> dict[str, str]:
    return {
        "contact_channel": "email",
        "email": address,
    }


def phone_contact(number: str, kind: str = "mobile", preference: str = "call") -> dict[str, str]:
    return {
        "contact_channel": "phone",
        "phone_number": number,
        "phone_kind": kind,
        "contact_preference": preference,
    }


def add_status(cannon: Laz0rCannon, description: str, request: str, status: str) -> None:
    cannon += Beam(
        description=description,
        request=request,
        response=[f"HTTP/1.1 {status}"],
        shutdown=socket.SHUT_WR,
    )


def raw_request(request: bytes) -> bytes:
    with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as connection:
        connection.settimeout(TIMEOUT)
        connection.sendall(request)
        connection.shutdown(socket.SHUT_WR)

        chunks: list[bytes] = []
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)

    return b"".join(chunks)


def split_response(response: bytes) -> tuple[str, dict[str, str], bytes]:
    header_bytes, separator, body = response.partition(b"\r\n\r\n")
    if not separator:
        raise AssertionError("Response enthält kein vollständiges HTTP-Headerende")

    lines = header_bytes.decode("iso-8859-1").split("\r\n")
    status = lines[0]
    headers: dict[str, str] = {}

    for line in lines[1:]:
        name, separator, value = line.partition(":")
        if not separator:
            raise AssertionError(f"Ungültige Response-Headerzeile: {line!r}")
        headers[name.strip().lower()] = value.strip()

    return status, headers, body


def assert_status(response: bytes, expected: str) -> tuple[dict[str, str], bytes]:
    status, headers, body = split_response(response)
    actual = status.removeprefix("HTTP/1.1 ")
    if actual != expected:
        raise AssertionError(f"Erwartet {expected}, erhalten {status}")
    return headers, body


def run_stateful_tests() -> int:
    failures = 0

    def check(name: str, function) -> None:
        nonlocal failures
        print(f"* Stateful: {name}")
        try:
            function()
        except Exception as error:  # noqa: BLE001 - test runner should report all failures
            failures += 1
            print(f"    \033[31mFailed: {error}\033[0m\n")
        else:
            print("    \033[32mOk\033[0m\n")

    def content_length_and_head() -> None:
        get_response = raw_request(request_text("GET", "/").encode())
        get_headers, get_body = assert_status(get_response, "200 OK")
        declared = int(get_headers["content-length"])
        if declared != len(get_body):
            raise AssertionError(f"GET Content-Length={declared}, tatsächlicher Body={len(get_body)}")

        head_response = raw_request(request_text("HEAD", "/").encode())
        head_headers, head_body = assert_status(head_response, "200 OK")
        if head_body != b"":
            raise AssertionError(f"HEAD enthält unerwartet {len(head_body)} Body-Bytes")
        if int(head_headers["content-length"]) != declared:
            raise AssertionError("HEAD und GET melden unterschiedliche Content-Length")

    def booking_persistence() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)

        with sqlite3.connect(database_file) as connection:
            schema_version = connection.execute("PRAGMA user_version").fetchone()[0]
            if schema_version != 7:
                raise AssertionError(f"Erwartete Kalender-Schemaversion 7, erhalten {schema_version}")

            required_tables = {
                "services",
                "calendar_settings",
                "weekly_opening_hours",
                "calendar_closures",
                "notification_jobs",
                "notification_templates",
                "gallery_images",
            }
            existing_tables = {
                row[0]
                for row in connection.execute(
                    "SELECT name FROM sqlite_master WHERE type = 'table'"
                )
            }
            if not required_tables.issubset(existing_tables):
                missing = sorted(required_tables - existing_tables)
                raise AssertionError(f"Kalendertabellen fehlen: {missing}")

            before_count = connection.execute(
                "SELECT COUNT(*) FROM bookings"
            ).fetchone()[0]

            legacy_row = connection.execute(
                "SELECT status, customer_name, contact, dog_name, message, legacy, "
                "       decision_status, appointment_date "
                "FROM bookings WHERE customer_name = ?",
                ("Legacy Test",),
            ).fetchone()

            if legacy_row != (
                "altbestand",
                "Legacy Test",
                "legacy@example.invalid",
                "Waldi",
                "Frühere Anfrage",
                1,
                "legacy",
                None,
            ):
                raise AssertionError("Das frühere fünfspaltige TSV-Format wurde nicht korrekt importiert")

            imported_v2 = connection.execute(
                "SELECT status, dog_size, service, preferred_date, legacy, "
                "       decision_status, appointment_date "
                "FROM bookings WHERE customer_name = ?",
                ("TSV V2 Test",),
            ).fetchone()

            if imported_v2 != (
                "neu",
                "small",
                "wash_dry",
                "2026-08-12",
                0,
                "legacy",
                None,
            ):
                raise AssertionError("Das TSV-v2-Format wurde nicht korrekt importiert")

            migration_marker = connection.execute(
                "SELECT value FROM app_metadata WHERE key = ?",
                ("legacy_tsv_import_v1",),
            ).fetchone()

            if migration_marker is None or not migration_marker[0].startswith("completed:2"):
                raise AssertionError("Der einmalige TSV-Import wurde nicht als abgeschlossen markiert")

    def contact_form_layout() -> None:
        response = raw_request(request_text("GET", "/kontakt").encode())
        _, body = assert_status(response, "200 OK")
        page = body.decode("utf-8")

        contact_position = page.find("Wie dürfen wir dich kontaktieren?")
        first_name_position = page.find('name="first_name"')
        last_name_position = page.find('name="last_name"')
        dog_name_position = page.find('name="dog_name"')

        if min(contact_position, first_name_position, last_name_position, dog_name_position) < 0:
            raise AssertionError("Kontaktformular enthält nicht alle erwarteten Felder")
        if not contact_position < first_name_position < last_name_position < dog_name_position:
            raise AssertionError("Vor- und Nachname stehen nicht unter der Kontaktwahl")
        if ('class="contact-channel-option"' not in page or
            'contact-channel-option-text-nowrap">E-Mail</span>' not in page):
            raise AssertionError("Kontaktarten verwenden nicht die bruchsichere Darstellung")
        if 'name="name"' in page:
            raise AssertionError("Das alte gemeinsame Namensfeld wird noch ausgeliefert")

    def public_calendar_and_pending_booking() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)
        target = datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=2)
        target_date = target.date().isoformat()
        target_weekday = target.isoweekday()

        with sqlite3.connect(database_file) as connection:
            before_count = connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0]
            connection.execute("DELETE FROM weekly_opening_hours")
            connection.execute(
                "INSERT INTO weekly_opening_hours(weekday, start_minute, end_minute) "
                "VALUES(?, 540, 780)",
                (target_weekday,),
            )
            connection.execute(
                "UPDATE calendar_settings "
                "SET min_notice_minutes = 0, booking_horizon_days = 90, "
                "    slot_interval_minutes = 15, pending_hold_minutes = 1440 "
                "WHERE id = 1"
            )

        services_response = raw_request(request_text("GET", "/api/services").encode())
        services_headers, services_body = assert_status(services_response, "200 OK")
        if services_headers.get("content-type") != "application/json; charset=utf-8":
            raise AssertionError("Leistungs-API liefert keinen JSON-Content-Type")

        services = json.loads(services_body)
        if services.get("timezone") != "Europe/Berlin" or not services.get("current_date"):
            raise AssertionError("Leistungs-API liefert keine Salonzeit")
        if services.get("booking_horizon_days") != 90:
            raise AssertionError("Leistungs-API liefert den Buchungshorizont nicht")
        service_codes = {service["code"] for service in services["services"]}
        if "full_groom" not in service_codes or "other" in service_codes:
            raise AssertionError("Aktive und deaktivierte Leistungen werden nicht korrekt gefiltert")

        query = urlencode({
            "service": "full_groom",
            "from": target_date,
            "to": target_date,
        })
        availability_response = raw_request(request_text(
            "GET",
            f"/api/availability?{query}",
        ).encode())
        _, availability_body = assert_status(availability_response, "200 OK")
        availability = json.loads(availability_body)

        if availability["timezone"] != "Europe/Berlin":
            raise AssertionError("Verfügbarkeits-API liefert die falsche Zeitzone")
        if len(availability["days"]) != 1 or availability["days"][0]["date"] != target_date:
            raise AssertionError("Verfügbarkeits-API liefert den falschen Tag")

        free_slots = [
            slot for slot in availability["days"][0]["slots"] if slot["available"]
        ]
        if not free_slots:
            raise AssertionError("Der konfigurierte Öffnungstag enthält keine freien Slots")

        selected_slot = free_slots[0]
        body = urlencode({
            "first_name": "Pew Pew",
            "last_name": "Test",
            **email_contact("test@example.invalid"),
            "dog_name": "Bello",
            "dog_size": "medium",
            "service": "full_groom",
            "appointment_date": target_date,
            "appointment_start": selected_slot["start"],
            "message": "Zeile 1\nZeile 2",
            "privacy_consent": "accepted",
        })
        response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.31",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            body,
        ).encode())
        assert_status(response, "201 Created")

        with sqlite3.connect(database_file) as connection:
            after_count = connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0]
            row = connection.execute(
                "SELECT status, customer_name, contact, dog_name, dog_size, service, "
                "       preferred_date, message, legacy, decision_status, appointment_date, "
                "       start_minute, end_minute, blocked_until_minute, hold_expires_at, "
                "       service_id IS NOT NULL, contact_channel, email, phone_number, "
                "       phone_kind, contact_preference, service_name_snapshot, "
                "       service_duration_minutes_snapshot, service_buffer_minutes_snapshot "
                "FROM bookings ORDER BY id DESC LIMIT 1"
            ).fetchone()

        if after_count != before_count + 1:
            raise AssertionError("Terminanfrage wurde nicht genau einmal gespeichert")

        if row[:11] != (
            "neu",
            "Pew Pew Test",
            "test@example.invalid",
            "Bello",
            "medium",
            "full_groom",
            target_date,
            "Zeile 1\nZeile 2",
            0,
            "pending",
            target_date,
        ):
            raise AssertionError(f"Pending-Termin wurde nicht korrekt gespeichert: {row!r}")

        if row[11] < 0 or row[12] <= row[11] or row[13] < row[12] or not row[14] or row[15] != 1:
            raise AssertionError(f"Gespeicherte Terminzeiten sind inkonsistent: {row!r}")
        if row[16:21] != ("email", "test@example.invalid", "", "", ""):
            raise AssertionError(f"Strukturierter E-Mail-Kontakt wurde falsch gespeichert: {row!r}")
        if row[21] != "Komplettpflege" or row[22] != 120 or row[23] != 15:
            raise AssertionError(f"Leistungssnapshot wurde falsch gespeichert: {row!r}")

        updated_response = raw_request(request_text(
            "GET",
            f"/api/availability?{query}",
        ).encode())
        _, updated_body = assert_status(updated_response, "200 OK")
        updated = json.loads(updated_body)
        selected_after = next(
            slot for slot in updated["days"][0]["slots"]
            if slot["start"] == selected_slot["start"]
        )
        if selected_after["available"]:
            raise AssertionError("Vorläufig reservierter Termin wird weiterhin als frei angezeigt")

        duplicate_response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.32",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            body,
        ).encode())
        assert_status(duplicate_response, "409 Conflict")

    def first_run_admin_setup() -> None:
        setup_response = raw_request(request_text("GET", "/setup/admin").encode())
        setup_headers, setup_body = assert_status(setup_response, "200 OK")

        if setup_headers.get("cache-control") != "no-store":
            raise AssertionError("Setup-Seite setzt Cache-Control: no-store nicht")

        match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', setup_body)
        if match is None:
            raise AssertionError("CSRF-Token wurde nicht in der Setup-Seite gefunden")

        username = TEST_ADMIN_USERNAME
        password = TEST_ADMIN_PASSWORD
        body = urlencode({
            "csrf_token": match.group(1).decode("ascii"),
            "username": username,
            "password": password,
            "password_repeat": password,
        })
        create_response = raw_request(request_text(
            "POST",
            "/setup/admin",
            {"Content-Type": "application/x-www-form-urlencoded"},
            body,
        ).encode())
        assert_status(create_response, "201 Created")

        hidden_response = raw_request(request_text("GET", "/setup/admin").encode())
        assert_status(hidden_response, "404 Not Found")

        wrong_token = base64.b64encode(f"{username}:wrong-password".encode()).decode()
        wrong_response = raw_request(request_text(
            "GET",
            "/admin/bookings",
            {"Authorization": f"Basic {wrong_token}"},
        ).encode())
        assert_status(wrong_response, "401 Unauthorized")

        valid_token = base64.b64encode(f"{username}:{password}".encode()).decode()
        valid_response = raw_request(request_text(
            "GET",
            "/admin/bookings",
            {"Authorization": f"Basic {valid_token}"},
        ).encode())
        valid_headers, valid_body = assert_status(valid_response, "200 OK")
        if not valid_headers.get("content-type", "").startswith("text/html"):
            raise AssertionError("Adminseite liefert keinen HTML Content-Type")
        if b"Buchungsanfragen" not in valid_body:
            raise AssertionError("Adminseite enthält die erwartete Überschrift nicht")
        if b"Komplettpflege" not in valid_body or b"Mittel" not in valid_body:
            raise AssertionError("Adminseite zeigt die erweiterten Buchungsfelder nicht an")
        if "Angefragt – vorläufig reserviert".encode("utf-8") not in valid_body or b"Uhrzeit" not in valid_body:
            raise AssertionError("Adminseite zeigt den Pending-Termin nicht verständlich an")
        if b"Legacy Test" not in valid_body or "Frühere Anfrage".encode("utf-8") not in valid_body:
            raise AssertionError("Adminseite liest das frühere fünfspaltige Format nicht mehr")
        if b'action="/admin/bookings/status"' not in valid_body:
            raise AssertionError("Adminseite enthält kein Formular zur Statusänderung")

    def admin_gallery_workflow() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}

        public_page_response = raw_request(request_text("GET", "/galerie").encode())
        _, public_page_body = assert_status(public_page_response, "200 OK")
        if b"/gallery.js" not in public_page_body or b"Styling 4 Dogs" not in public_page_body:
            raise AssertionError("Öffentliche Galerieseite ist unvollständig")

        empty_api_response = raw_request(request_text("GET", "/api/gallery").encode())
        _, empty_api_body = assert_status(empty_api_response, "200 OK")
        if json.loads(empty_api_body) != []:
            raise AssertionError("Neue Galerie ist nicht leer")

        admin_response = raw_request(request_text(
            "GET",
            "/admin/gallery",
            auth_headers,
        ).encode())
        _, admin_body = assert_status(admin_response, "200 OK")
        csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', admin_body)
        if csrf_match is None:
            raise AssertionError("Galerie-Adminseite enthält kein CSRF-Token")
        if b'action="/admin/gallery/upload"' not in admin_body:
            raise AssertionError("Galerie-Adminseite enthält kein Upload-Formular")
        if b'/logo.jpg' not in admin_body:
            raise AssertionError("Galerie-Adminseite verwendet das neue Logo nicht")

        csrf_token = csrf_match.group(1).decode("ascii")
        image_bytes = base64.b64decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
        )
        boundary = "----Styling4DogsGalleryTestBoundary"
        parts: list[bytes] = []

        def add_text(name: str, value: str) -> None:
            parts.append(
                f"--{boundary}\r\n"
                f"Content-Disposition: form-data; name=\"{name}\"\r\n\r\n"
                f"{value}\r\n".encode("utf-8")
            )

        add_text("csrf_token", csrf_token)
        add_text("title", "Bello nach der Pflege")
        add_text("alt_text", "Kleiner Hund mit frisch geschnittenem Fell")
        add_text("sort_order", "10")
        add_text("visible", "1")
        add_text("publication_consent", "1")
        parts.append(
            f"--{boundary}\r\n"
            "Content-Disposition: form-data; name=\"image\"; filename=\"bello.png\"\r\n"
            "Content-Type: image/png\r\n\r\n".encode("ascii")
            + image_bytes
            + b"\r\n"
        )
        parts.append(f"--{boundary}--\r\n".encode("ascii"))
        upload_body = b"".join(parts)
        upload_request = (
            f"POST /admin/gallery/upload HTTP/1.1\r\n"
            f"Host: {HOST}:{PORT}\r\n"
            f"Authorization: Basic {auth_token}\r\n"
            f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
            f"Content-Length: {len(upload_body)}\r\n\r\n"
        ).encode("ascii") + upload_body
        upload_response = raw_request(upload_request)
        upload_headers, _ = assert_status(upload_response, "303 See Other")
        if upload_headers.get("location") != "/admin/gallery?saved=uploaded":
            raise AssertionError("Galerie-Upload leitet nicht korrekt zurück")

        with sqlite3.connect(database_file) as connection:
            row = connection.execute(
                "SELECT id, title, alt_text, file_name, mime_type, length(image_data), sort_order, visible "
                "FROM gallery_images ORDER BY id DESC LIMIT 1"
            ).fetchone()
        if row is None:
            raise AssertionError("Galeriebild wurde nicht in SQLite gespeichert")
        if row[1:3] != (
            "Bello nach der Pflege",
            "Kleiner Hund mit frisch geschnittenem Fell",
        ):
            raise AssertionError(f"Galerietexte wurden falsch gespeichert: {row!r}")
        if row[4] != "image/png" or row[5] != len(image_bytes) or row[6:] != (10, 1):
            raise AssertionError(f"Galeriebild-Metadaten sind inkonsistent: {row!r}")

        image_id = row[0]
        file_name = row[3]
        api_response = raw_request(request_text("GET", "/api/gallery").encode())
        _, api_body = assert_status(api_response, "200 OK")
        items = json.loads(api_body)
        if len(items) != 1 or items[0]["title"] != "Bello nach der Pflege":
            raise AssertionError(f"Galerie-API liefert falsche Daten: {items!r}")
        if items[0]["url"] != f"/media/{file_name}":
            raise AssertionError("Galerie-API liefert eine falsche Bild-URL")

        media_response = raw_request(request_text("GET", f"/media/{file_name}").encode())
        media_headers, media_body = assert_status(media_response, "200 OK")
        if media_headers.get("content-type") != "image/png":
            raise AssertionError("Galeriebild liefert falschen Content-Type")
        if media_body != image_bytes:
            raise AssertionError("Galeriebild wurde nicht binär korrekt ausgeliefert")

        delete_body = urlencode({
            "csrf_token": csrf_token,
            "image_id": str(image_id),
        })
        delete_response = raw_request(request_text(
            "POST",
            "/admin/gallery/delete",
            {
                **auth_headers,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            delete_body,
        ).encode())
        delete_headers, _ = assert_status(delete_response, "303 See Other")
        if delete_headers.get("location") != "/admin/gallery?saved=deleted":
            raise AssertionError("Galerie-Löschen leitet nicht korrekt zurück")

        with sqlite3.connect(database_file) as connection:
            remaining = connection.execute("SELECT COUNT(*) FROM gallery_images").fetchone()[0]
        if remaining != 0:
            raise AssertionError("Galeriebild wurde nicht gelöscht")

    def admin_status_workflow() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}

        admin_response = raw_request(request_text(
            "GET",
            "/admin/bookings",
            auth_headers,
        ).encode())
        admin_headers, admin_body = assert_status(admin_response, "200 OK")

        if admin_headers.get("cache-control") != "no-store":
            raise AssertionError("Adminseite setzt Cache-Control: no-store nicht")

        csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', admin_body)
        if csrf_match is None:
            raise AssertionError("CSRF-Token wurde im Adminbereich nicht gefunden")
        csrf_token = csrf_match.group(1).decode("ascii")

        with sqlite3.connect(database_file) as connection:
            target = connection.execute(
                "SELECT id, status FROM bookings WHERE customer_name = ?",
                ("Pew Pew Test",),
            ).fetchone()
            untouched = connection.execute(
                "SELECT id, status FROM bookings WHERE customer_name = ?",
                ("TSV V2 Test",),
            ).fetchone()

        if target is None or untouched is None:
            raise AssertionError("Testbuchungen für die Statusänderung fehlen")

        target_id, original_status = target
        untouched_id, untouched_status = untouched

        missing_auth_body = urlencode({
            "csrf_token": csrf_token,
            "booking_id": str(target_id),
            "status": "kontaktiert",
        })
        missing_auth_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {"Content-Type": "application/x-www-form-urlencoded"},
            missing_auth_body,
        ).encode())
        assert_status(missing_auth_response, "401 Unauthorized")

        invalid_csrf_body = urlencode({
            "csrf_token": "0" * 64,
            "booking_id": str(target_id),
            "status": "kontaktiert",
        })
        invalid_csrf_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {
                **auth_headers,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            invalid_csrf_body,
        ).encode())
        assert_status(invalid_csrf_response, "403 Forbidden")

        invalid_status_body = urlencode({
            "csrf_token": csrf_token,
            "booking_id": str(target_id),
            "status": "geloescht",
        })
        invalid_status_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {
                **auth_headers,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            invalid_status_body,
        ).encode())
        assert_status(invalid_status_response, "400 Bad Request")

        invalid_id_body = urlencode({
            "csrf_token": csrf_token,
            "booking_id": "12x",
            "status": "kontaktiert",
        })
        invalid_id_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {
                **auth_headers,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            invalid_id_body,
        ).encode())
        assert_status(invalid_id_response, "400 Bad Request")

        unknown_id_body = urlencode({
            "csrf_token": csrf_token,
            "booking_id": "9223372036854775807",
            "status": "kontaktiert",
        })
        unknown_id_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {
                **auth_headers,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            unknown_id_body,
        ).encode())
        assert_status(unknown_id_response, "404 Not Found")

        update_body = urlencode({
            "csrf_token": csrf_token,
            "booking_id": str(target_id),
            "status": "kontaktiert",
        })
        update_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {
                **auth_headers,
                "Content-Type": "application/x-www-form-urlencoded",
            },
            update_body,
        ).encode())
        update_headers, _ = assert_status(update_response, "303 See Other")

        if update_headers.get("location") != "/admin/bookings":
            raise AssertionError("Statusänderung leitet nicht zum Adminbereich zurück")

        with sqlite3.connect(database_file) as connection:
            updated_status = connection.execute(
                "SELECT status FROM bookings WHERE id = ?",
                (target_id,),
            ).fetchone()[0]
            current_untouched_status = connection.execute(
                "SELECT status FROM bookings WHERE id = ?",
                (untouched_id,),
            ).fetchone()[0]

        if updated_status != "kontaktiert":
            raise AssertionError(f"Status wurde nicht gespeichert: {updated_status!r}")
        if current_untouched_status != untouched_status:
            raise AssertionError("Eine andere Buchung wurde unerwartet verändert")
        if original_status == updated_status:
            raise AssertionError("Der Zielstatus hat sich nicht verändert")

        refreshed_response = raw_request(request_text(
            "GET",
            "/admin/bookings",
            auth_headers,
        ).encode())
        _, refreshed_body = assert_status(refreshed_response, "200 OK")

        selected_pattern = (
            rb'<option value="kontaktiert" selected>Kontaktiert</option>'
        )
        if re.search(selected_pattern, refreshed_body) is None:
            raise AssertionError("Adminseite zeigt den gespeicherten Status nicht als ausgewählt")

    def admin_filter_workflow() -> None:
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}

        status_query = urlencode({"status": "kontaktiert"})
        status_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{status_query}",
            auth_headers,
        ).encode())
        _, status_body = assert_status(status_response, "200 OK")

        if b"Pew Pew Test" not in status_body:
            raise AssertionError("Statusfilter zeigt die kontaktierte Buchung nicht")
        if b"TSV V2 Test" in status_body or b"Legacy Test" in status_body:
            raise AssertionError("Statusfilter zeigt Buchungen mit anderem Status")
        if b'<option value="kontaktiert" selected>' not in status_body:
            raise AssertionError("Gewählter Statusfilter wird nicht beibehalten")
        if b'class="admin-summary"' not in status_body:
            raise AssertionError("Adminübersicht enthält keine Statuszähler")

        dog_query = urlencode({"q": "Waldi"})
        dog_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{dog_query}",
            auth_headers,
        ).encode())
        _, dog_body = assert_status(dog_response, "200 OK")

        if b"Legacy Test" not in dog_body or b"Pew Pew Test" in dog_body:
            raise AssertionError("Hundesuche filtert die Buchungen nicht korrekt")

        contact_query = urlencode({"q": "v2@example.invalid"})
        contact_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{contact_query}",
            auth_headers,
        ).encode())
        _, contact_body = assert_status(contact_response, "200 OK")

        if b"TSV V2 Test" not in contact_body or b"Legacy Test" in contact_body:
            raise AssertionError("Kontaktsuche filtert die Buchungen nicht korrekt")

        literal_wildcard_query = urlencode({"q": "%"})
        literal_wildcard_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{literal_wildcard_query}",
            auth_headers,
        ).encode())
        _, literal_wildcard_body = assert_status(literal_wildcard_response, "200 OK")

        if "Für diesen Filter wurden keine Buchungsanfragen gefunden.".encode("utf-8") not in literal_wildcard_body:
            raise AssertionError("LIKE-Sonderzeichen werden nicht als wörtliche Suche behandelt")

        invalid_status_response = raw_request(request_text(
            "GET",
            "/admin/bookings?status=geloescht",
            auth_headers,
        ).encode())
        assert_status(invalid_status_response, "400 Bad Request")

        duplicate_status_response = raw_request(request_text(
            "GET",
            "/admin/bookings?status=neu&status=erledigt",
            auth_headers,
        ).encode())
        assert_status(duplicate_status_response, "400 Bad Request")

        invalid_encoding_response = raw_request(request_text(
            "GET",
            "/admin/bookings?q=%ZZ",
            auth_headers,
        ).encode())
        assert_status(invalid_encoding_response, "400 Bad Request")

        escaped_query = urlencode({"q": '<script>alert("x")</script>'})
        escaped_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{escaped_query}",
            auth_headers,
        ).encode())
        _, escaped_body = assert_status(escaped_response, "200 OK")

        if b'<script>alert("x")</script>' in escaped_body:
            raise AssertionError("Suchwert wird ungeescaped in die Adminseite geschrieben")
        if b'&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt;' not in escaped_body:
            raise AssertionError("Suchwert wird nicht sicher im Formular wiedergegeben")


    def admin_booking_decisions() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {
            **auth_headers,
            "Content-Type": "application/x-www-form-urlencoded",
        }

        page_response = raw_request(request_text(
            "GET",
            "/admin/bookings",
            auth_headers,
        ).encode())
        _, page_body = assert_status(page_response, "200 OK")
        csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', page_body)
        if csrf_match is None:
            raise AssertionError("CSRF-Token wurde im Buchungsbereich nicht gefunden")
        csrf_token = csrf_match.group(1).decode("ascii")

        with sqlite3.connect(database_file) as connection:
            pending = connection.execute(
                "SELECT id, appointment_date, service FROM bookings "
                "WHERE decision_status = 'pending' ORDER BY id LIMIT 1"
            ).fetchone()
        if pending is None:
            raise AssertionError("Keine offene Terminanfrage für den Annahmetest vorhanden")
        pending_id, pending_date, pending_service = pending

        if b'action="/admin/bookings/accept"' not in page_body or b'action="/admin/bookings/reject"' not in page_body:
            raise AssertionError("Annehmen- und Ablehnen-Aktionen fehlen im Adminbereich")

        accept_response = raw_request(request_text(
            "POST",
            "/admin/bookings/accept",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "booking_id": str(pending_id),
            }),
        ).encode())
        accept_headers, _ = assert_status(accept_response, "303 See Other")
        if accept_headers.get("location") != "/admin/bookings":
            raise AssertionError("Terminannahme leitet nicht korrekt zurück")

        with sqlite3.connect(database_file) as connection:
            accepted = connection.execute(
                "SELECT decision_status, hold_expires_at, decision_at "
                "FROM bookings WHERE id = ?",
                (pending_id,),
            ).fetchone()
        if accepted is None or accepted[0] != "confirmed" or accepted[1] is not None or not accepted[2]:
            raise AssertionError(f"Terminannahme wurde falsch gespeichert: {accepted!r}")

        repeated_response = raw_request(request_text(
            "POST",
            "/admin/bookings/accept",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "booking_id": str(pending_id),
            }),
        ).encode())
        assert_status(repeated_response, "409 Conflict")

        next_date = (datetime.fromisoformat(pending_date) + timedelta(days=7)).date().isoformat()
        availability_query = urlencode({
            "service": pending_service,
            "from": next_date,
            "to": next_date,
        })
        availability_response = raw_request(request_text(
            "GET",
            f"/api/availability?{availability_query}",
        ).encode())
        _, availability_body = assert_status(availability_response, "200 OK")
        availability = json.loads(availability_body)
        free_slots = [slot for slot in availability["days"][0]["slots"] if slot["available"]]
        if not free_slots:
            raise AssertionError("Kein freier Folgetermin für den Ablehnungstest vorhanden")
        selected_slot = free_slots[0]

        phone_body = urlencode({
            "name": "WhatsApp Kundin",
            **phone_contact("+49 170 1234567", "mobile", "whatsapp"),
            "dog_name": "Luna",
            "dog_size": "small",
            "service": pending_service,
            "appointment_date": next_date,
            "appointment_start": selected_slot["start"],
            "message": "Bitte per WhatsApp melden",
            "privacy_consent": "accepted",
        })
        phone_response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.61",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            phone_body,
        ).encode())
        assert_status(phone_response, "201 Created")

        with sqlite3.connect(database_file) as connection:
            phone_booking = connection.execute(
                "SELECT id, decision_status, contact_channel, phone_number, phone_kind, "
                "       contact_preference FROM bookings "
                "WHERE customer_name = 'WhatsApp Kundin' ORDER BY id DESC LIMIT 1"
            ).fetchone()
        if phone_booking is None or phone_booking[1:] != (
            "pending", "phone", "+49 170 1234567", "mobile", "whatsapp"
        ):
            raise AssertionError(f"WhatsApp-Kontakt wurde falsch gespeichert: {phone_booking!r}")

        reject_response = raw_request(request_text(
            "POST",
            "/admin/bookings/reject",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "booking_id": str(phone_booking[0]),
                "rejection_reason": "Der Termin ist intern nicht möglich",
            }),
        ).encode())
        assert_status(reject_response, "303 See Other")

        with sqlite3.connect(database_file) as connection:
            rejected = connection.execute(
                "SELECT decision_status, hold_expires_at, rejection_reason "
                "FROM bookings WHERE id = ?",
                (phone_booking[0],),
            ).fetchone()
        if rejected != ("rejected", None, "Der Termin ist intern nicht möglich"):
            raise AssertionError(f"Terminablehnung wurde falsch gespeichert: {rejected!r}")

        released_response = raw_request(request_text(
            "GET",
            f"/api/availability?{availability_query}",
        ).encode())
        _, released_body = assert_status(released_response, "200 OK")
        released = json.loads(released_body)
        released_slot = next(
            slot for slot in released["days"][0]["slots"]
            if slot["start"] == selected_slot["start"]
        )
        if not released_slot["available"]:
            raise AssertionError("Abgelehnter Termin wurde nicht wieder freigegeben")


    def admin_calendar_workflow() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {
            **auth_headers,
            "Content-Type": "application/x-www-form-urlencoded",
        }

        page_response = raw_request(request_text(
            "GET",
            "/admin/calendar",
            auth_headers,
        ).encode())
        page_headers, page_body = assert_status(page_response, "200 OK")

        if page_headers.get("cache-control") != "no-store":
            raise AssertionError("Admin-Kalender setzt Cache-Control: no-store nicht")
        for expected in (
            "Kalender verwalten",
            "Buchungsregeln",
            "Regelmäßige Öffnungszeiten",
            "Leistungen und Dauer",
            "Urlaub und Sperrzeiten",
            "Leistung hinzufügen",
            "automatisch verbindlich bestätigen",
            "Frühester buchbarer Termin",
            "Freihaltezeit für offene Anfragen",
            "Alle Einstellungen speichern",
            "/admin/calendar/save-all",
            "/admin-calendar.js",
            "Buchungsschutz",
        ):
            if expected.encode("utf-8") not in page_body:
                raise AssertionError(f"Admin-Kalender enthält {expected!r} nicht")

        csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', page_body)
        if csrf_match is None:
            raise AssertionError("CSRF-Token wurde im Admin-Kalender nicht gefunden")
        csrf_token = csrf_match.group(1).decode("ascii")

        invalid_csrf_response = raw_request(request_text(
            "POST",
            "/admin/calendar/settings",
            form_headers,
            urlencode({
                "csrf_token": "0" * 64,
                "min_notice_hours": "0",
                "booking_horizon_days": "120",
                "slot_interval_minutes": "30",
                "pending_hold_hours": "12",
                "reminder_lead_hours": "24",
                "reminder_enabled": "1",
            }),
        ).encode())
        assert_status(invalid_csrf_response, "403 Forbidden")

        settings_response = raw_request(request_text(
            "POST",
            "/admin/calendar/settings",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "min_notice_hours": "0",
                "booking_horizon_days": "120",
                "slot_interval_minutes": "30",
                "pending_hold_hours": "12",
                "reminder_lead_hours": "24",
                "reminder_enabled": "1",
            }),
        ).encode())
        settings_headers, _ = assert_status(settings_response, "303 See Other")
        if settings_headers.get("location") != "/admin/calendar?saved=settings":
            raise AssertionError("Buchungsregeln leiten nicht korrekt zurück")

        target = datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=14)
        target_date = target.date().isoformat()
        target_weekday = target.isoweekday()

        with sqlite3.connect(database_file) as connection:
            service_rows = connection.execute(
                "SELECT code, name, duration_minutes, buffer_minutes, active "
                "FROM services ORDER BY sort_order, code"
            ).fetchall()

        save_all_body = {
            "csrf_token": csrf_token,
            "min_notice_hours": "0",
            "booking_horizon_days": "120",
            "slot_interval_minutes": "30",
            "pending_hold_hours": "12",
            "reminder_lead_hours": "24",
            "reminder_enabled": "1",
            f"day_{target_weekday}_start_1": "09:00",
            f"day_{target_weekday}_end_1": "12:00",
            f"day_{target_weekday}_start_2": "13:00",
            f"day_{target_weekday}_end_2": "17:00",
        }
        for code, name, duration, buffer, active in service_rows:
            save_all_body[f"service_{code}_name"] = name
            save_all_body[f"service_{code}_duration"] = str(duration)
            save_all_body[f"service_{code}_buffer"] = str(buffer)
            if active:
                save_all_body[f"service_{code}_active"] = "1"

        save_all_response = raw_request(request_text(
            "POST",
            "/admin/calendar/save-all",
            form_headers,
            urlencode(save_all_body),
        ).encode())
        save_all_headers, _ = assert_status(save_all_response, "303 See Other")
        if save_all_headers.get("location") != "/admin/calendar?saved=all":
            raise AssertionError("Gemeinsames Speichern leitet nicht korrekt zurück")

        with sqlite3.connect(database_file) as connection:
            stored_periods = connection.execute(
                "SELECT start_minute, end_minute FROM weekly_opening_hours "
                "WHERE weekday = ? ORDER BY start_minute",
                (target_weekday,),
            ).fetchall()
        if stored_periods != [(540, 720), (780, 1020)]:
            raise AssertionError(f"Gemeinsames Speichern hat Öffnungszeiten falsch ersetzt: {stored_periods!r}")

        hours_body = {
            "csrf_token": csrf_token,
            "weekday": str(target_weekday),
            "start_1": "09:00",
            "end_1": "12:00",
            "start_2": "13:00",
            "end_2": "17:00",
            "start_3": "",
            "end_3": "",
            "start_4": "",
            "end_4": "",
        }
        hours_response = raw_request(request_text(
            "POST",
            "/admin/calendar/hours",
            form_headers,
            urlencode(hours_body),
        ).encode())
        hours_headers, _ = assert_status(hours_response, "303 See Other")
        if hours_headers.get("location") != "/admin/calendar?saved=hours":
            raise AssertionError("Öffnungszeiten leiten nicht korrekt zurück")

        overlapping_body = {
            **hours_body,
            "start_2": "11:30",
            "end_2": "14:00",
        }
        overlap_response = raw_request(request_text(
            "POST",
            "/admin/calendar/hours",
            form_headers,
            urlencode(overlapping_body),
        ).encode())
        assert_status(overlap_response, "400 Bad Request")

        service_response = raw_request(request_text(
            "POST",
            "/admin/calendar/service",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "code": "full_groom",
                "name": "Komplettpflege Premium",
                "duration_minutes": "90",
                "buffer_minutes": "30",
                "active": "1",
            }),
        ).encode())
        service_headers, _ = assert_status(service_response, "303 See Other")
        if service_headers.get("location") != "/admin/calendar?saved=service":
            raise AssertionError("Leistungsänderung leitet nicht korrekt zurück")

        deactivate_response = raw_request(request_text(
            "POST",
            "/admin/calendar/service",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "code": "consultation",
                "name": "Beratung",
                "duration_minutes": "30",
                "buffer_minutes": "0",
            }),
        ).encode())
        assert_status(deactivate_response, "303 See Other")

        add_service_response = raw_request(request_text(
            "POST",
            "/admin/calendar/service/add",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "code": "coat_consult",
                "name": "Fellberatung",
                "duration_minutes": "45",
                "buffer_minutes": "15",
                "active": "1",
            }),
        ).encode())
        add_service_headers, _ = assert_status(add_service_response, "303 See Other")
        if add_service_headers.get("location") != "/admin/calendar?saved=service-added":
            raise AssertionError("Neue Leistung leitet nicht korrekt zurück")

        services_response = raw_request(request_text("GET", "/api/services").encode())
        _, services_body = assert_status(services_response, "200 OK")
        services = json.loads(services_body)["services"]
        service_by_code = {service["code"]: service for service in services}
        if "consultation" in service_by_code:
            raise AssertionError("Deaktivierte Beratung wird weiterhin öffentlich angeboten")
        if service_by_code.get("full_groom", {}).get("name") != "Komplettpflege Premium":
            raise AssertionError("Geänderter Leistungsname erscheint nicht in der API")
        if service_by_code.get("coat_consult", {}).get("name") != "Fellberatung":
            raise AssertionError("Neu angelegte Leistung erscheint nicht in der API")

        delete_service_response = raw_request(request_text(
            "POST",
            "/admin/calendar/service/delete",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "code": "coat_consult",
            }),
        ).encode())
        delete_service_headers, _ = assert_status(delete_service_response, "303 See Other")
        if delete_service_headers.get("location") != "/admin/calendar?saved=service-deleted":
            raise AssertionError("Leistungslöschung leitet nicht korrekt zurück")

        services_after_delete_response = raw_request(request_text("GET", "/api/services").encode())
        _, services_after_delete_body = assert_status(services_after_delete_response, "200 OK")
        deleted_codes = {item["code"] for item in json.loads(services_after_delete_body)["services"]}
        if "coat_consult" in deleted_codes:
            raise AssertionError("Gelöschte unbenutzte Leistung wird weiterhin angeboten")

        availability_query = urlencode({
            "service": "full_groom",
            "from": target_date,
            "to": target_date,
        })
        before_closure_response = raw_request(request_text(
            "GET",
            f"/api/availability?{availability_query}",
        ).encode())
        _, before_closure_body = assert_status(before_closure_response, "200 OK")
        before_closure = json.loads(before_closure_body)
        if not any(slot["available"] for slot in before_closure["days"][0]["slots"]):
            raise AssertionError("Gespeicherte Öffnungszeiten erzeugen keine freien Termine")

        closure_response = raw_request(request_text(
            "POST",
            "/admin/calendar/closure/add",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "start_date": target_date,
                "end_date": target_date,
                "start_time": "",
                "end_time": "",
                "label": "Testurlaub",
                "all_day": "1",
            }),
        ).encode())
        closure_headers, _ = assert_status(closure_response, "303 See Other")
        if closure_headers.get("location") != "/admin/calendar?saved=closure-added":
            raise AssertionError("Sperrzeit leitet nicht korrekt zurück")

        after_closure_response = raw_request(request_text(
            "GET",
            f"/api/availability?{availability_query}",
        ).encode())
        _, after_closure_body = assert_status(after_closure_response, "200 OK")
        after_closure = json.loads(after_closure_body)
        if any(slot["available"] for slot in after_closure["days"][0]["slots"]):
            raise AssertionError("Ganztägige Sperrzeit blockiert nicht alle Termine")

        with sqlite3.connect(database_file) as connection:
            settings = connection.execute(
                "SELECT min_notice_minutes, booking_horizon_days, "
                "slot_interval_minutes, pending_hold_minutes, auto_confirm_bookings, "
                "email_notifications_enabled, reminder_enabled, reminder_lead_minutes "
                "FROM calendar_settings WHERE id = 1"
            ).fetchone()
            periods = connection.execute(
                "SELECT start_minute, end_minute FROM weekly_opening_hours "
                "WHERE weekday = ? ORDER BY start_minute",
                (target_weekday,),
            ).fetchall()
            service = connection.execute(
                "SELECT name, duration_minutes, buffer_minutes, active "
                "FROM services WHERE code = 'full_groom'"
            ).fetchone()
            closure_row = connection.execute(
                "SELECT id FROM calendar_closures "
                "WHERE start_date = ? AND end_date = ? AND label = ?",
                (target_date, target_date, "Testurlaub"),
            ).fetchone()

        if settings != (0, 120, 30, 720, 0, 0, 1, 1440):
            raise AssertionError(f"Buchungsregeln wurden falsch gespeichert: {settings!r}")
        if periods != [(540, 720), (780, 1020)]:
            raise AssertionError(f"Öffnungszeiten wurden falsch gespeichert: {periods!r}")
        if service != ("Komplettpflege Premium", 90, 30, 1):
            raise AssertionError(f"Leistung wurde falsch gespeichert: {service!r}")
        if closure_row is None:
            raise AssertionError("Sperrzeit wurde nicht in SQLite gespeichert")

        delete_response = raw_request(request_text(
            "POST",
            "/admin/calendar/closure/delete",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "closure_id": str(closure_row[0]),
            }),
        ).encode())
        delete_headers, _ = assert_status(delete_response, "303 See Other")
        if delete_headers.get("location") != "/admin/calendar?saved=closure-deleted":
            raise AssertionError("Sperrzeit-Löschung leitet nicht korrekt zurück")

        restored_response = raw_request(request_text(
            "GET",
            f"/api/availability?{availability_query}",
        ).encode())
        _, restored_body = assert_status(restored_response, "200 OK")
        restored = json.loads(restored_body)
        if not any(slot["available"] for slot in restored["days"][0]["slots"]):
            raise AssertionError("Gelöschte Sperrzeit gibt Termine nicht wieder frei")

        notice_response = raw_request(request_text(
            "GET",
            "/admin/calendar?saved=settings",
            auth_headers,
        ).encode())
        _, notice_body = assert_status(notice_response, "200 OK")
        if "Die Buchungsregeln wurden gespeichert.".encode("utf-8") not in notice_body:
            raise AssertionError("Admin-Kalender zeigt keine Speicherbestätigung")


    def booking_spam_protection() -> None:
        database_file = Path(os.environ["STYLES4DOGS_TEST_DATABASE_FILE"])

        with sqlite3.connect(database_file) as connection:
            before_honeypot = connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0]

        honeypot_response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.170",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            urlencode({"company_website": "https://spam.example.invalid"}),
        ).encode())
        assert_status(honeypot_response, "201 Created")

        with sqlite3.connect(database_file) as connection:
            after_honeypot = connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0]
        if after_honeypot != before_honeypot:
            raise AssertionError("Ausgefülltes Honeypot-Feld wurde als echte Buchung gespeichert")

        target = datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=28)
        target_date = target.date().isoformat()
        query = urlencode({
            "service": "claw_care",
            "from": target_date,
            "to": target_date,
        })
        contact_email = "spam-schutz@example.invalid"
        booking_headers = proxy_headers(
            "198.51.100.171",
            {"Content-Type": "application/x-www-form-urlencoded"},
        )

        for index in range(4):
            availability_response = raw_request(request_text(
                "GET",
                f"/api/availability?{query}",
            ).encode())
            _, availability_body = assert_status(availability_response, "200 OK")
            payload = json.loads(availability_body)
            free_slots = [slot for slot in payload["days"][0]["slots"] if slot["available"]]
            if not free_slots:
                raise AssertionError("Nicht genügend freie Slots für den Kontakt-Spamschutz")

            booking_body = urlencode({
                "first_name": "Spam",
                "last_name": f"Schutz {index + 1}",
                **email_contact(contact_email),
                "dog_name": f"Hund {index + 1}",
                "dog_size": "small",
                "service": "claw_care",
                "appointment_date": target_date,
                "appointment_start": free_slots[0]["start"],
                "message": "Kontaktlimit-Test",
                "privacy_consent": "accepted",
            })
            response = raw_request(request_text(
                "POST",
                "/booking",
                booking_headers,
                booking_body,
            ).encode())

            if index < 3:
                assert_status(response, "201 Created")
            else:
                headers, body = assert_status(response, "429 Too Many Requests")
                if headers.get("retry-after") != "86400":
                    raise AssertionError("Kontaktlimit setzt nicht Retry-After: 86400")
                if "Buchungsschutz" not in body.decode("utf-8"):
                    raise AssertionError("Kontaktlimit erklärt die Ablehnung nicht")

        with sqlite3.connect(database_file) as connection:
            stored = connection.execute(
                "SELECT COUNT(*) FROM bookings WHERE lower(email) = lower(?)",
                (contact_email,),
            ).fetchone()[0]
        if stored != 3:
            raise AssertionError(f"Kontaktlimit speicherte {stored} statt genau drei Anfragen")


    def admin_email_connection() -> None:
        database_file = Path(os.environ["STYLES4DOGS_TEST_DATABASE_FILE"])
        secrets_dir = Path(os.environ["STYLES4DOGS_TEST_SECRETS_DIR"])
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {**auth_headers, "Content-Type": "application/x-www-form-urlencoded"}

        response = raw_request(request_text("GET", "/admin/notifications", auth_headers).encode())
        headers, body = assert_status(response, "200 OK")
        if headers.get("cache-control") != "no-store":
            raise AssertionError("E-Mail-Adminseite setzt Cache-Control: no-store nicht")
        for expected in (
            "E-Mail-Konto verbinden",
            "Automatische Nachrichten individualisieren",
            "Terminbestätigung",
            "Terminabsage",
            "{{customer_name}}",
        ):
            if expected.encode("utf-8") not in body:
                raise AssertionError(f"E-Mail-Adminseite enthält {expected!r} nicht")

        match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', body)
        if match is None:
            raise AssertionError("CSRF-Token auf E-Mail-Adminseite fehlt")
        csrf_token = match.group(1).decode("ascii")
        password = "smtp-app-password-test"

        response = raw_request(request_text(
            "POST",
            "/admin/notifications/smtp",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "smtp_url": "smtps://smtp.example.invalid:465",
                "smtp_username": "salon@example.invalid",
                "smtp_password": password,
                "from_address": "salon@example.invalid",
                "from_name": "Styling 4 Dogs",
                "admin_email": "admin@example.invalid",
                "notify_admin_on_new_booking": "1",
            }),
        ).encode())
        response_headers, _ = assert_status(response, "303 See Other")
        if response_headers.get("location") != "/admin/notifications?saved=smtp":
            raise AssertionError("SMTP-Speicherung leitet falsch zurück")

        smtp_file = secrets_dir / "notification.smtp"
        key_file = secrets_dir / "notification.key"
        if not smtp_file.is_file() or not key_file.is_file():
            raise AssertionError("Verschlüsselte SMTP-Dateien fehlen")
        if smtp_file.stat().st_mode & 0o777 != 0o600 or key_file.stat().st_mode & 0o777 != 0o600:
            raise AssertionError("SMTP-Secrets besitzen nicht den Modus 0600")
        if password.encode() in smtp_file.read_bytes():
            raise AssertionError("SMTP-Passwort wurde unverschlüsselt gespeichert")

        response = raw_request(request_text(
            "POST",
            "/admin/notifications/test",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "recipient_email": "admin@example.invalid",
            }),
        ).encode())
        assert_status(response, "303 See Other")

        with sqlite3.connect(database_file) as connection:
            row = connection.execute(
                "SELECT booking_id, recipient_email, status FROM notification_jobs "
                "WHERE event_type='smtp_test' ORDER BY id DESC LIMIT 1"
            ).fetchone()
        if row != (None, "admin@example.invalid", "pending"):
            raise AssertionError(f"Testmail wurde falsch eingereiht: {row!r}")



    def automatic_confirmation() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")
        database_file = Path(database_file_value)

        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {
            **auth_headers,
            "Content-Type": "application/x-www-form-urlencoded",
        }
        page_response = raw_request(request_text("GET", "/admin/calendar", auth_headers).encode())
        _, page_body = assert_status(page_response, "200 OK")
        csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', page_body)
        if csrf_match is None:
            raise AssertionError("CSRF-Token für automatische Bestätigung fehlt")
        csrf_token = csrf_match.group(1).decode("ascii")

        enable_response = raw_request(request_text(
            "POST",
            "/admin/calendar/settings",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "min_notice_hours": "0",
                "booking_horizon_days": "120",
                "slot_interval_minutes": "30",
                "pending_hold_hours": "12",
                "reminder_lead_hours": "168",
                "auto_confirm_bookings": "1",
                "email_notifications_enabled": "1",
                "reminder_enabled": "1",
            }),
        ).encode())
        assert_status(enable_response, "303 See Other")

        services_response = raw_request(request_text("GET", "/api/services").encode())
        _, services_body = assert_status(services_response, "200 OK")
        services_payload = json.loads(services_body)
        if services_payload.get("automatic_confirmation") is not True:
            raise AssertionError("Leistungs-API meldet die aktivierte Bestätigungsautomatik nicht")

        target = datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=14)
        target_date = target.date().isoformat()
        query = urlencode({
            "service": "full_groom",
            "from": target_date,
            "to": target_date,
        })
        availability_response = raw_request(request_text("GET", f"/api/availability?{query}").encode())
        _, availability_body = assert_status(availability_response, "200 OK")
        availability = json.loads(availability_body)
        free_slots = [slot for slot in availability["days"][0]["slots"] if slot["available"]]
        if not free_slots:
            raise AssertionError("Kein freier Termin für automatische Bestätigung vorhanden")
        selected_slot = free_slots[0]

        booking_body = urlencode({
            "name": "Auto Bestätigung",
            **email_contact("auto@example.invalid"),
            "dog_name": "Milo",
            "dog_size": "medium",
            "service": "full_groom",
            "appointment_date": target_date,
            "appointment_start": selected_slot["start"],
            "message": "Automatiktest",
            "privacy_consent": "accepted",
        })
        booking_response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.62",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            booking_body,
        ).encode())
        _, confirmation_body = assert_status(booking_response, "201 Created")
        if "automatisch bestätigt".encode("utf-8") not in confirmation_body:
            raise AssertionError("Kundenseite zeigt die automatische Bestätigung nicht an")

        with sqlite3.connect(database_file) as connection:
            row = connection.execute(
                "SELECT id, decision_status, hold_expires_at, decision_at, contact_channel, email "
                "FROM bookings WHERE customer_name = 'Auto Bestätigung' "
                "ORDER BY id DESC LIMIT 1"
            ).fetchone()
            if row is None:
                raise AssertionError("Automatisch bestätigter Termin fehlt")
            notification = connection.execute(
                "SELECT status, recipient_email, subject, body_text, ics_content "
                "FROM notification_jobs WHERE booking_id = ? AND event_type = 'booking_confirmed'",
                (row[0],),
            ).fetchone()

            reminder_date = (datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=1)).date().isoformat()
            connection.execute(
                "UPDATE bookings SET appointment_date = ? WHERE id = ?",
                (reminder_date, row[0]),
            )
            connection.commit()

        if row[1] != "confirmed" or row[2] is not None or not row[3] or row[4:] != (
            "email", "auto@example.invalid"
        ):
            raise AssertionError(f"Automatisch bestätigter Termin wurde falsch gespeichert: {row!r}")
        if notification is None or notification[0] != "pending" or notification[1] != "auto@example.invalid":
            raise AssertionError(f"Bestätigungs-E-Mail wurde nicht eingereiht: {notification!r}")
        if "Termin bestätigt" not in notification[2] or "Auto Bestätigung" not in notification[3]:
            raise AssertionError("Bestätigungs-E-Mail enthält nicht die erwarteten Termindaten")
        if "BEGIN:VCALENDAR" not in notification[4] or "BEGIN:VEVENT" not in notification[4]:
            raise AssertionError("Bestätigungs-E-Mail enthält keine gültige Kalenderdatei")

        disable_response = raw_request(request_text(
            "POST",
            "/admin/calendar/settings",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "min_notice_hours": "0",
                "booking_horizon_days": "120",
                "slot_interval_minutes": "30",
                "pending_hold_hours": "12",
                "reminder_lead_hours": "168",
                "email_notifications_enabled": "1",
                "reminder_enabled": "1",
            }),
        ).encode())
        assert_status(disable_response, "303 See Other")


    def admin_message_templates() -> None:
        database_file = Path(os.environ["STYLES4DOGS_TEST_DATABASE_FILE"])
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {**auth_headers, "Content-Type": "application/x-www-form-urlencoded"}

        response = raw_request(request_text("GET", "/admin/notifications", auth_headers).encode())
        _, body = assert_status(response, "200 OK")
        match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', body)
        if match is None:
            raise AssertionError("CSRF-Token für Vorlagen fehlt")
        csrf_token = match.group(1).decode("ascii")

        subject = "Dein Termin für {{dog_name}} ist bestätigt"
        message = (
            "Hallo {{customer_name}},\n\n"
            "dein Termin am {{appointment_date}} von {{start_time}} bis {{end_time}} Uhr "
            "für {{service_name}} ist fest eingetragen.\n\nViele Grüße\n{{salon_name}}"
        )
        response = raw_request(request_text(
            "POST",
            "/admin/notifications/template",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "event_type": "booking_confirmed",
                "subject_template": subject,
                "body_template": message,
            }),
        ).encode())
        assert_status(response, "303 See Other")

        response = raw_request(request_text(
            "POST",
            "/admin/notifications/template",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "event_type": "booking_rejected",
                "subject_template": "Rückmeldung zu deiner Terminanfrage",
                "body_template": "Hallo {{customer_name}},\n{{rejection_reason}}\n{{salon_name}}",
            }),
        ).encode())
        assert_status(response, "303 See Other")

        invalid = raw_request(request_text(
            "POST",
            "/admin/notifications/template",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "event_type": "booking_confirmed",
                "subject_template": "Ungültig {{nicht_erlaubt}}",
                "body_template": "Text",
            }),
        ).encode())
        assert_status(invalid, "400 Bad Request")

        with sqlite3.connect(database_file) as connection:
            stored = connection.execute(
                "SELECT subject_template, body_template FROM notification_templates "
                "WHERE event_type='booking_confirmed'"
            ).fetchone()
            admin_job = connection.execute(
                "SELECT recipient_email, status, subject, body_text FROM notification_jobs "
                "WHERE event_type='admin_new_booking' ORDER BY id DESC LIMIT 1"
            ).fetchone()
            connection.execute(
                "UPDATE notification_jobs SET status='failed', attempts=5, last_error='Test' "
                "WHERE event_type='smtp_test'"
            )
            connection.commit()

        if stored != (subject, message):
            raise AssertionError("Individualisierte Bestätigung wurde nicht gespeichert")
        if admin_job is None or admin_job[0] != "admin@example.invalid" or admin_job[1] != "pending":
            raise AssertionError(f"Admin-Benachrichtigung fehlt: {admin_job!r}")
        if "Auto Bestätigung" not in admin_job[3]:
            raise AssertionError("Admin-Benachrichtigung enthält Kundendaten nicht")

        retry = raw_request(request_text(
            "POST",
            "/admin/notifications/retry",
            form_headers,
            urlencode({"csrf_token": csrf_token}),
        ).encode())
        assert_status(retry, "303 See Other")
        with sqlite3.connect(database_file) as connection:
            retried = connection.execute(
                "SELECT status, attempts, last_error FROM notification_jobs "
                "WHERE event_type='smtp_test' ORDER BY id DESC LIMIT 1"
            ).fetchone()
        if retried != ("pending", 0, ""):
            raise AssertionError(f"Fehlgeschlagene Mail wurde nicht freigegeben: {retried!r}")

        with sqlite3.connect(database_file) as connection:
            connection.execute(
                "UPDATE notification_jobs SET status='sent', sent_at='2026-07-21T12:00:00Z' "
                "WHERE id = (SELECT id FROM notification_jobs ORDER BY id LIMIT 1)"
            )
            connection.execute(
                "UPDATE notification_jobs SET status='failed', attempts=5, last_error='Endgültiger Testfehler' "
                "WHERE id = (SELECT id FROM notification_jobs ORDER BY id DESC LIMIT 1)"
            )
            connection.commit()

        clear_sent = raw_request(request_text(
            "POST",
            "/admin/notifications/clear-sent",
            form_headers,
            urlencode({"csrf_token": csrf_token}),
        ).encode())
        clear_sent_headers, _ = assert_status(clear_sent, "303 See Other")
        if clear_sent_headers.get("location") != "/admin/notifications?saved=clear-sent":
            raise AssertionError("Zurücksetzen gesendeter Nachrichten leitet falsch zurück")

        clear_failed = raw_request(request_text(
            "POST",
            "/admin/notifications/clear-failed",
            form_headers,
            urlencode({"csrf_token": csrf_token}),
        ).encode())
        clear_failed_headers, _ = assert_status(clear_failed, "303 See Other")
        if clear_failed_headers.get("location") != "/admin/notifications?saved=clear-failed":
            raise AssertionError("Löschen fehlgeschlagener Nachrichten leitet falsch zurück")

        with sqlite3.connect(database_file) as connection:
            sent_count = connection.execute(
                "SELECT COUNT(*) FROM notification_jobs WHERE status='sent'"
            ).fetchone()[0]
            failed_count = connection.execute(
                "SELECT COUNT(*) FROM notification_jobs WHERE status='failed'"
            ).fetchone()[0]
        if sent_count != 0 or failed_count != 0:
            raise AssertionError(
                f"Warteschlangen-Zähler wurden nicht bereinigt: sent={sent_count}, failed={failed_count}"
            )


    def admin_appointments_workflow() -> None:
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")

        database_file = Path(database_file_value)
        with sqlite3.connect(database_file) as connection:
            row = connection.execute(
                "SELECT appointment_date, id FROM bookings "
                "WHERE customer_name = 'Auto Bestätigung' ORDER BY id DESC LIMIT 1"
            ).fetchone()
        if row is None:
            raise AssertionError("Testtermin für die Adminansicht fehlt")

        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        response = raw_request(request_text(
            "GET",
            f"/admin/appointments?view=day&date={row[0]}",
            auth_headers,
        ).encode())
        headers, body = assert_status(response, "200 OK")
        if headers.get("cache-control") != "no-store":
            raise AssertionError("Admin-Terminkalender setzt Cache-Control: no-store nicht")

        page = body.decode("utf-8")
        parsed_date = datetime.strptime(row[0], "%Y-%m-%d")
        german_weekdays = [
            "Montag", "Dienstag", "Mittwoch", "Donnerstag",
            "Freitag", "Samstag", "Sonntag",
        ]
        expected_display_date = (
            f"{parsed_date:%d.%m.%Y} - {german_weekdays[parsed_date.weekday()]}"
        )
        if expected_display_date not in page:
            raise AssertionError(
                f"Termindatum wird nicht deutsch mit Wochentag dargestellt: {expected_display_date}"
            )

        for expected in (
            "Terminkalender",
            "Auto Bestätigung",
            "Milo",
            "Komplettpflege Premium",
            "E-Mail schreiben",
            f"/admin/bookings?search={row[1]}",
            "Versandbereit",
        ):
            if expected not in page:
                raise AssertionError(f"Admin-Terminkalender enthält {expected!r} nicht")

        week_response = raw_request(request_text(
            "GET",
            f"/admin/appointments?view=week&date={row[0]}",
            auth_headers,
        ).encode())
        _, week_body = assert_status(week_response, "200 OK")
        if "Woche" not in week_body.decode("utf-8"):
            raise AssertionError("Wochenansicht wurde nicht ausgeliefert")

        month_response = raw_request(request_text(
            "GET",
            f"/admin/appointments?view=month&date={row[0]}",
            auth_headers,
        ).encode())
        _, month_body = assert_status(month_response, "200 OK")
        month_page = month_body.decode("utf-8")
        for expected in ("30 Tage", "Bestätigter Termin", "Sperrzeit oder Urlaub"):
            if expected not in month_page:
                raise AssertionError(f"30-Tage-Ansicht enthält {expected!r} nicht")



    def rate_limiting() -> None:
        invalid_body = urlencode({"name": "Rate Limit"})
        booking_headers = proxy_headers(
            "198.51.100.200",
            {"Content-Type": "application/x-www-form-urlencoded"},
        )

        for _ in range(5):
            response = raw_request(request_text(
                "POST",
                "/booking",
                booking_headers,
                invalid_body,
            ).encode())
            assert_status(response, "400 Bad Request")

        limited_response = raw_request(request_text(
            "POST",
            "/booking",
            booking_headers,
            invalid_body,
        ).encode())
        limited_headers, _ = assert_status(limited_response, "429 Too Many Requests")
        retry_after = int(limited_headers.get("retry-after", "0"))
        if retry_after < 1 or retry_after > 600:
            raise AssertionError(f"Ungültiger Retry-After-Wert: {retry_after}")

        # A caller who does not know the proxy secret must not evade the limit
        # by rotating a forged X-Forwarded-For header.
        for index in range(5):
            spoofed_headers = {
                "Content-Type": "application/x-www-form-urlencoded",
                "X-Forwarded-For": f"203.0.113.{index + 1}",
                "X-Styles4Dogs-Proxy-Token": "falsches-token-mit-ausreichender-laenge-000000000000",
            }
            response = raw_request(request_text(
                "POST",
                "/booking",
                spoofed_headers,
                invalid_body,
            ).encode())
            assert_status(response, "400 Bad Request")

        spoofed_limited = raw_request(request_text(
            "POST",
            "/booking",
            {
                "Content-Type": "application/x-www-form-urlencoded",
                "X-Forwarded-For": "203.0.113.250",
                "X-Styles4Dogs-Proxy-Token": "falsches-token-mit-ausreichender-laenge-000000000000",
            },
            invalid_body,
        ).encode())
        assert_status(spoofed_limited, "429 Too Many Requests")

        invalid_admin_headers = proxy_headers(
            "198.51.100.201",
            {"Authorization": "Basic !!!"},
        )
        for _ in range(10):
            response = raw_request(request_text(
                "GET",
                "/admin/bookings",
                invalid_admin_headers,
            ).encode())
            assert_status(response, "401 Unauthorized")

        blocked_admin = raw_request(request_text(
            "GET",
            "/admin/bookings",
            invalid_admin_headers,
        ).encode())
        assert_status(blocked_admin, "429 Too Many Requests")

        # Successful authentication clears earlier failures for that client.
        reset_ip = "198.51.100.202"
        reset_bad_headers = proxy_headers(
            reset_ip,
            {"Authorization": "Basic !!!"},
        )
        for _ in range(9):
            response = raw_request(request_text(
                "GET",
                "/admin/bookings",
                reset_bad_headers,
            ).encode())
            assert_status(response, "401 Unauthorized")

        valid_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        reset_valid_headers = proxy_headers(
            reset_ip,
            {"Authorization": f"Basic {valid_token}"},
        )
        valid_response = raw_request(request_text(
            "GET",
            "/admin/bookings",
            reset_valid_headers,
        ).encode())
        assert_status(valid_response, "200 OK")

        for _ in range(10):
            response = raw_request(request_text(
                "GET",
                "/admin/bookings",
                reset_bad_headers,
            ).encode())
            assert_status(response, "401 Unauthorized")

        reset_blocked = raw_request(request_text(
            "GET",
            "/admin/bookings",
            reset_bad_headers,
        ).encode())
        assert_status(reset_blocked, "429 Too Many Requests")

        # A separate global safety valve eventually rejects even requests from
        # rotating client addresses. Existing test requests already count, so
        # the exact iteration is intentionally not hard-coded.
        global_limited = None
        for index in range(1, 121):
            third_octet = index // 250
            fourth_octet = index % 250 + 1
            headers = proxy_headers(
                f"198.18.{third_octet}.{fourth_octet}",
                {"Content-Type": "application/x-www-form-urlencoded"},
            )
            response = raw_request(request_text(
                "POST",
                "/booking",
                headers,
                invalid_body,
            ).encode())
            status, response_headers, _ = split_response(response)
            if status == "HTTP/1.1 429 Too Many Requests":
                global_limited = response_headers
                break
            if status != "HTTP/1.1 400 Bad Request":
                raise AssertionError(f"Unerwartete globale Limit-Antwort: {status}")

        if global_limited is None:
            raise AssertionError("Globale Buchungs-Notbremse wurde nicht ausgelöst")
        global_retry = int(global_limited.get("retry-after", "0"))
        if global_retry < 1 or global_retry > 60:
            raise AssertionError(f"Ungültiger globaler Retry-After-Wert: {global_retry}")

    check("GET/HEAD Content-Length und leerer HEAD-Body", content_length_and_head)
    check("TSV-Altbestand und Kalenderschema werden korrekt geladen", booking_persistence)
    check("Kontaktformular trennt Vor- und Nachname", contact_form_layout)
    check("Öffentlicher Kalender reserviert einen Pending-Termin", public_calendar_and_pending_booking)
    check("Einmaliges Admin-Setup und Basic Auth", first_run_admin_setup)
    check("Admin lädt Galeriebilder hoch und löscht sie", admin_gallery_workflow)
    check("Admin kann einen Buchungsstatus sicher ändern", admin_status_workflow)
    check("Admin kann Buchungen filtern und durchsuchen", admin_filter_workflow)
    check("Admin nimmt Terminanfragen an oder lehnt sie ab", admin_booking_decisions)
    check("Admin speichert Kalendereinstellungen gemeinsam", admin_calendar_workflow)
    check("Honeypot und Kontaktlimit schützen vor Quatschbuchungen", booking_spam_protection)
    check("Admin verbindet ein E-Mail-Konto und reiht eine Testmail ein", admin_email_connection)
    check("Freie Termine können automatisch bestätigt werden", automatic_confirmation)
    check("Admin individualisiert Bestätigungen, Absagen und bereinigt die Queue", admin_message_templates)
    check("Admin sieht Termine in Tages-, Wochen- und 30-Tage-Ansicht", admin_appointments_workflow)
    check("Rate-Limits schützen Buchung und Adminzugang", rate_limiting)

    return failures


def main() -> int:
    cannon = Laz0rCannon(host=HOST, port=PORT, timeout=TIMEOUT)

    public_routes = [
        ("Startseite", "/"),
        ("Leistungen", "/leistungen"),
        ("Preise", "/preise"),
        ("Galerie", "/galerie"),
        ("Kontakt", "/kontakt"),
        ("Impressum", "/impressum"),
        ("Datenschutz", "/datenschutz"),
        ("Stylesheet", "/style.css"),
        ("Kalender-JavaScript", "/calendar.js"),
        ("Admin-Kalender-JavaScript", "/admin-calendar.js"),
        ("Galerie-JavaScript", "/gallery.js"),
        ("Logo", "/logo.jpg"),
    ]

    for name, path in public_routes:
        add_status(cannon, f"GET {name}", request_text("GET", path), "200 OK")

    add_status(cannon, "GET aktive Leistungen", request_text("GET", "/api/services"), "200 OK")
    add_status(cannon, "Verfügbarkeit ohne Parameter", request_text("GET", "/api/availability"), "400 Bad Request")

    add_status(cannon, "Query-String wird von Route getrennt", request_text("GET", "/kontakt?quelle=test"), "200 OK")
    add_status(cannon, "Unbekannte Datei", request_text("GET", "/gibt-es-nicht.html"), "404 Not Found")
    add_status(cannon, "Directory Traversal", request_text("GET", "/../../CMakeLists.txt"), "403 Forbidden")
    add_status(cannon, "Request ohne Resource", "GET HTTP/1.1\r\nHost: localhost\r\n\r\n", "400 Bad Request")
    add_status(cannon, "Request ohne Methode", "/ HTTP/1.1\r\nHost: localhost\r\n\r\n", "400 Bad Request")
    add_status(cannon, "Request ohne HTTP-Version", "GET /\r\nHost: localhost\r\n\r\n", "400 Bad Request")
    add_status(cannon, "Nicht unterstützte HTTP-Version", "GET / HTTP/2.0\r\nHost: localhost\r\n\r\n", "400 Bad Request")
    add_status(cannon, "Nicht erlaubte Methode", request_text("PUT", "/"), "405 Method Not Allowed")
    add_status(cannon, "POST auf unbekannte Route", request_text("POST", "/unbekannt"), "404 Not Found")
    add_status(cannon, "Adminbereich ohne Zugangsdaten", request_text("GET", "/admin/bookings"), "401 Unauthorized")
    add_status(cannon, "Admin-Kalender ohne Zugangsdaten", request_text("GET", "/admin/calendar"), "401 Unauthorized")
    add_status(cannon, "Admin-Terminkalender ohne Zugangsdaten", request_text("GET", "/admin/appointments"), "401 Unauthorized")
    add_status(cannon, "Admin-Galerie ohne Zugangsdaten", request_text(
        "GET",
        "/admin/gallery",
        proxy_headers("198.51.100.90"),
    ), "401 Unauthorized")
    add_status(cannon, "Adminbereich mit ungültigem Basic Token", request_text(
        "GET", "/admin/bookings", {"Authorization": "Basic !!!"}
    ), "401 Unauthorized")
    add_status(cannon, "Statusänderung ohne Zugangsdaten", request_text(
        "POST",
        "/admin/bookings/status",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "booking_id=1&status=kontaktiert&csrf_token=invalid",
    ), "401 Unauthorized")
    add_status(cannon, "Kalenderänderung ohne Zugangsdaten", request_text(
        "POST",
        "/admin/calendar/settings",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "min_notice_hours=0&booking_horizon_days=90&slot_interval_minutes=15&pending_hold_hours=24&reminder_lead_hours=24&csrf_token=invalid",
    ), "401 Unauthorized")
    add_status(cannon, "Terminannahme ohne Zugangsdaten", request_text(
        "POST",
        "/admin/bookings/accept",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "booking_id=1&csrf_token=invalid",
    ), "401 Unauthorized")
    add_status(cannon, "Leistung anlegen ohne Zugangsdaten", request_text(
        "POST",
        "/admin/calendar/service/add",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "code=test&name=Test&duration_minutes=30&buffer_minutes=0&csrf_token=invalid",
    ), "401 Unauthorized")
    add_status(cannon, "Setup-Seite beim ersten Start", request_text("GET", "/setup/admin"), "200 OK")

    missing_slot_booking = urlencode({
        "name": "Laz0r Test",
        **email_contact("laz0r@example.invalid"),
        "dog_name": "Flocke",
        "dog_size": "small",
        "service": "wash_dry",
        "message": "Regressionstest",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Terminanfrage ohne ausgewählten Slot", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.41",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        missing_slot_booking,
    ), "400 Bad Request")

    invalid_booking = urlencode({"name": "Ohne Kontakt"})
    add_status(cannon, "Buchung ohne Pflichtfeld", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.42",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        invalid_booking,
    ), "400 Bad Request")

    missing_privacy = urlencode({
        "name": "Ohne Zustimmung",
        **email_contact("privacy@example.invalid"),
        "dog_size": "medium",
        "service": "full_groom",
    })
    add_status(cannon, "Buchung ohne Datenschutzbestätigung", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.43",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        missing_privacy,
    ), "400 Bad Request")

    invalid_service = urlencode({
        "name": "Ungültige Leistung",
        **email_contact("service@example.invalid"),
        "dog_size": "medium",
        "service": "nicht-erlaubt",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Buchung mit ungültiger Leistung", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.44",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        invalid_service,
    ), "400 Bad Request")

    invalid_date = urlencode({
        "name": "Ungültiges Datum",
        **email_contact("date@example.invalid"),
        "dog_size": "medium",
        "service": "full_groom",
        "appointment_date": "2026-02-31",
        "appointment_start": "09:00",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Buchung mit ungültigem Wunschdatum", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.45",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        invalid_date,
    ), "400 Bad Request")

    invalid_whatsapp_landline = urlencode({
        "name": "Ungültiger WhatsApp-Kontakt",
        **phone_contact("02571 123456", "landline", "whatsapp"),
        "dog_size": "small",
        "service": "wash_dry",
        "appointment_date": "2026-08-20",
        "appointment_start": "09:00",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "WhatsApp mit Festnetznummer wird abgelehnt", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.46",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        invalid_whatsapp_landline,
    ), "400 Bad Request")

    invalid_email = urlencode({
        "name": "Ungültige E-Mail",
        **email_contact("keine-adresse"),
        "dog_size": "small",
        "service": "wash_dry",
        "appointment_date": "2026-08-20",
        "appointment_start": "09:00",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Ungültige E-Mail-Adresse wird abgelehnt", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.47",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        invalid_email,
    ), "400 Bad Request")

    pew_ok = cannon.pewpew()
    stateful_failures = run_stateful_tests()

    print("* Gesamtergebnis")
    print(f"    PewPewLaz0rTank-Fehler: {cannon.result['fail']}")
    print(f"    Stateful-Fehler:       {stateful_failures}")

    return 0 if pew_ok and stateful_failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
