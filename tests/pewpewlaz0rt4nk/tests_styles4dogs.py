#!/usr/bin/env python3
"""HTTP regression tests for the Styles 4 Dogs C server."""

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
            if schema_version != 3:
                raise AssertionError(f"Erwartete Kalender-Schemaversion 3, erhalten {schema_version}")

            required_tables = {
                "services",
                "calendar_settings",
                "weekly_opening_hours",
                "calendar_closures",
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
            "name": "Pew Pew Test",
            "contact": "test@example.invalid",
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
                "       service_id IS NOT NULL "
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
            }),
        ).encode())
        settings_headers, _ = assert_status(settings_response, "303 See Other")
        if settings_headers.get("location") != "/admin/calendar?saved=settings":
            raise AssertionError("Buchungsregeln leiten nicht korrekt zurück")

        target = datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=14)
        target_date = target.date().isoformat()
        target_weekday = target.isoweekday()

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

        services_response = raw_request(request_text("GET", "/api/services").encode())
        _, services_body = assert_status(services_response, "200 OK")
        services = json.loads(services_body)["services"]
        service_by_code = {service["code"]: service for service in services}
        if "consultation" in service_by_code:
            raise AssertionError("Deaktivierte Beratung wird weiterhin öffentlich angeboten")
        if service_by_code.get("full_groom", {}).get("name") != "Komplettpflege Premium":
            raise AssertionError("Geänderter Leistungsname erscheint nicht in der API")

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
                "slot_interval_minutes, pending_hold_minutes "
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

        if settings != (0, 120, 30, 720):
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
    check("Öffentlicher Kalender reserviert einen Pending-Termin", public_calendar_and_pending_booking)
    check("Einmaliges Admin-Setup und Basic Auth", first_run_admin_setup)
    check("Admin kann einen Buchungsstatus sicher ändern", admin_status_workflow)
    check("Admin kann Buchungen filtern und durchsuchen", admin_filter_workflow)
    check("Admin verwaltet Öffnungszeiten, Leistungen und Sperrzeiten", admin_calendar_workflow)
    check("Rate-Limits schützen Buchung und Adminzugang", rate_limiting)

    return failures


def main() -> int:
    cannon = Laz0rCannon(host=HOST, port=PORT, timeout=TIMEOUT)

    public_routes = [
        ("Startseite", "/"),
        ("Leistungen", "/leistungen"),
        ("Preise", "/preise"),
        ("Kontakt", "/kontakt"),
        ("Impressum", "/impressum"),
        ("Datenschutz", "/datenschutz"),
        ("Stylesheet", "/style.css"),
        ("Kalender-JavaScript", "/calendar.js"),
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
        "min_notice_hours=0&booking_horizon_days=90&slot_interval_minutes=15&pending_hold_hours=24&csrf_token=invalid",
    ), "401 Unauthorized")
    add_status(cannon, "Setup-Seite beim ersten Start", request_text("GET", "/setup/admin"), "200 OK")

    missing_slot_booking = urlencode({
        "name": "Laz0r Test",
        "contact": "laz0r@example.invalid",
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
        "contact": "privacy@example.invalid",
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
        "contact": "service@example.invalid",
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
        "contact": "date@example.invalid",
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

    pew_ok = cannon.pewpew()
    stateful_failures = run_stateful_tests()

    print("* Gesamtergebnis")
    print(f"    PewPewLaz0rTank-Fehler: {cannon.result['fail']}")
    print(f"    Stateful-Fehler:       {stateful_failures}")

    return 0 if pew_ok and stateful_failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
