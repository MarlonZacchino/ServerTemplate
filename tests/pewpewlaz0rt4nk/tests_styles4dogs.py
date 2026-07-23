#!/usr/bin/env python3
"""HTTP regression tests for the Styling 4 Dogs C server."""

from __future__ import annotations

import base64
import calendar
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


def address_fields(
    street_address: str = "Teststraße 12a",
    postal_code: str = "26121",
    city: str = "Oldenburg",
) -> dict[str, str]:
    return {
        "street_address": street_address,
        "postal_code": postal_code,
        "city": city,
        "dog_breed": "mixed_breed",
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
            if schema_version != 14:
                raise AssertionError(f"Erwartete Kalender-Schemaversion 14, erhalten {schema_version}")

            required_tables = {
                "services",
                "calendar_settings",
                "weekly_opening_hours",
                "calendar_closures",
                "notification_jobs",
                "notification_templates",
                "gallery_images",
                "customers",
                "dogs",
                "booking_events",
                "admin_users",
                "admin_sessions",
                "admin_login_attempts",
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
                "SELECT status, customer_name, contact, street_address, postal_code, city, dog_name, message, legacy, "
                "       decision_status, appointment_date "
                "FROM bookings WHERE customer_name = ?",
                ("Legacy Test",),
            ).fetchone()

            if legacy_row != (
                "neu",
                "Legacy Test",
                "legacy@example.invalid",
                "",
                "",
                "",
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
        street_position = page.find('name="street_address"')
        postal_position = page.find('name="postal_code"')
        city_position = page.find('name="city"')
        dog_name_position = page.find('name="dog_name"')
        dog_breed_position = page.find('name="dog_breed"')

        if min(
            contact_position,
            first_name_position,
            last_name_position,
            street_position,
            postal_position,
            city_position,
            dog_name_position,
            dog_breed_position,
        ) < 0:
            raise AssertionError("Kontaktformular enthält nicht alle erwarteten Felder")
        if not (
            contact_position
            < first_name_position
            < last_name_position
            < street_position
            < postal_position
            < city_position
            < dog_name_position
            < dog_breed_position
        ):
            raise AssertionError("Name und Pflichtadresse stehen nicht in der erwarteten Reihenfolge")
        for field_name in ("street_address", "postal_code", "city"):
            if re.search(
                rf'<input[^>]+name="{field_name}"[^>]+required',
                page,
            ) is None:
                raise AssertionError(f"Adressfeld {field_name!r} ist nicht verpflichtend")
        if re.search(r'<select[^>]+name="dog_breed"[^>]+required', page) is not None:
            raise AssertionError("Rasse wird weiterhin als Pflichtfeld ausgeliefert")
        if '<option value="">Keine Angabe</option>' not in page:
            raise AssertionError("Die optionale Rasse bietet keine eindeutige Leerauswahl")
        if 'value="other">Sonstiges</option>' not in page or 'data-dog-breed-other-hint' not in page:
            raise AssertionError("Sonstiges-Auswahl oder Rassehinweis fehlt")
        if 'pattern="[0-9]{5}"' not in page:
            raise AssertionError("PLZ-Formatprüfung fehlt")
        if '<script src="/postal-code.js" defer></script>' not in page:
            raise AssertionError("PLZ-Autovervollständigung wird nicht geladen")
        if ('class="contact-channel-option"' not in page or
            'contact-channel-option-text-nowrap">E-Mail</span>' not in page):
            raise AssertionError("Kontaktarten verwenden nicht die bruchsichere Darstellung")
        if 'name="name"' in page:
            raise AssertionError("Das alte gemeinsame Namensfeld wird noch ausgeliefert")

    def postal_code_lookup() -> None:
        response = raw_request(request_text(
            "GET",
            "/api/postal-code?postal_code=26121",
        ).encode())
        headers, body = assert_status(response, "200 OK")
        if headers.get("content-type") != "application/json; charset=utf-8":
            raise AssertionError("PLZ-API liefert keinen JSON-Content-Type")
        payload = json.loads(body)
        if payload != [{"name": "Oldenburg", "postalCode": "26121"}]:
            raise AssertionError(f"PLZ-API liefert unerwartete Daten: {payload!r}")

        multiple = raw_request(request_text(
            "GET",
            "/api/postal-code?postal_code=12345",
        ).encode())
        _, multiple_body = assert_status(multiple, "200 OK")
        multiple_payload = json.loads(multiple_body)
        if [item["name"] for item in multiple_payload] != [
            "Beispielstadt",
            "Beispieldorf",
        ]:
            raise AssertionError("PLZ-API liefert Mehrort-Ergebnisse nicht vollständig")

        invalid = raw_request(request_text(
            "GET",
            "/api/postal-code?postal_code=1234",
        ).encode())
        assert_status(invalid, "400 Bad Request")

        duplicate = raw_request(request_text(
            "GET",
            "/api/postal-code?postal_code=26121&postal_code=12345",
        ).encode())
        assert_status(duplicate, "400 Bad Request")

        head = raw_request(request_text(
            "HEAD",
            "/api/postal-code?postal_code=26121",
        ).encode())
        head_headers, head_body = assert_status(head, "200 OK")
        if head_body != b"" or int(head_headers["content-length"]) <= 0:
            raise AssertionError("HEAD der PLZ-API enthält einen Body oder keine Länge")

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
        booking_fields = {
            "first_name": "Pew Pew",
            "last_name": "Test",
            **email_contact("test@example.invalid"),
            **address_fields(),
            "dog_name": "Bello",
            "dog_size": "medium",
            "service": "full_groom",
            "appointment_date": target_date,
            "appointment_start": selected_slot["start"],
            "message": "Zeile 1\nZeile 2",
            "privacy_consent": "accepted",
        }
        booking_fields.pop("dog_breed")
        body = urlencode(booking_fields)
        response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.31",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            body,
        ).encode())
        _, booking_created_body = assert_status(response, "201 Created")
        portal_match = re.search(rb'href="[^" ]*(/buchung/([0-9]+)/[0-9a-f]{64})"', booking_created_body)
        if portal_match is None:
            raise AssertionError("Die Buchungsbestätigung enthält keinen persönlichen Kundenlink")

        with sqlite3.connect(database_file) as connection:
            after_count = connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0]
            row = connection.execute(
                "SELECT status, customer_name, contact, street_address, postal_code, city, dog_name, dog_size, service, "
                "       preferred_date, message, legacy, decision_status, appointment_date, "
                "       start_minute, end_minute, blocked_until_minute, hold_expires_at, "
                "       service_id IS NOT NULL, contact_channel, email, phone_number, "
                "       phone_kind, contact_preference, service_name_snapshot, "
                "       service_duration_minutes_snapshot, service_buffer_minutes_snapshot, dog_breed "
                "FROM bookings ORDER BY id DESC LIMIT 1"
            ).fetchone()

        if after_count != before_count + 1:
            raise AssertionError("Terminanfrage wurde nicht genau einmal gespeichert")

        if row[:14] != (
            "neu",
            "Pew Pew Test",
            "test@example.invalid",
            "Teststraße 12a",
            "26121",
            "Oldenburg",
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

        if row[14] < 0 or row[15] <= row[14] or row[16] < row[15] or not row[17] or row[18] != 1:
            raise AssertionError(f"Gespeicherte Terminzeiten sind inkonsistent: {row!r}")
        if row[19:24] != ("email", "test@example.invalid", "", "", ""):
            raise AssertionError(f"Strukturierter E-Mail-Kontakt wurde falsch gespeichert: {row!r}")
        if row[24] != "Komplettpflege" or row[25] != 120 or row[26] != 15:
            raise AssertionError(f"Leistungssnapshot wurde falsch gespeichert: {row!r}")
        if row[27] != "":
            raise AssertionError(f"Optionale Rasse wurde nicht leer gespeichert: {row!r}")

        portal_path = portal_match.group(1).decode("ascii")
        portal_response = raw_request(request_text("GET", portal_path).encode())
        portal_headers, portal_body = assert_status(portal_response, "200 OK")
        if portal_headers.get("cache-control") != "no-store":
            raise AssertionError("Persönlicher Kundenbereich darf nicht gecacht werden")
        if b"Bello" not in portal_body or "Wird vom Salon geprüft".encode("utf-8") not in portal_body:
            raise AssertionError("Persönlicher Kundenbereich zeigt Buchung oder Status nicht an")
        if b'href="/impressum">Zum Impressum</a>' not in portal_body:
            raise AssertionError("Kundenbereich verweist für Kontaktdaten nicht auf das Impressum")
        if b'href="/kontakt">Kontakt aufnehmen</a>' in portal_body:
            raise AssertionError("Kundenbereich verweist noch auf das Buchungsformular")

        invalid_portal = portal_path[:-1] + ("0" if portal_path[-1] != "0" else "1")
        invalid_response = raw_request(request_text("GET", invalid_portal).encode())
        assert_status(invalid_response, "404 Not Found")

        portal_key = Path(os.environ["STYLES4DOGS_TEST_SECRETS_DIR"]) / "customer-portal.key"
        if not portal_key.is_file() or portal_key.stat().st_mode & 0o777 != 0o600:
            raise AssertionError("Kundenbereich-Schlüssel fehlt oder besitzt nicht Modus 0600")

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
        assert_status(wrong_response, "303 See Other")

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

    def admin_session_workflow() -> None:
        client_ip = "198.51.100.45"

        def create_session(ip_address: str) -> tuple[str, str]:
            login_response = raw_request(request_text(
                "GET",
                "/admin/login",
                proxy_headers(ip_address),
            ).encode())
            login_headers, login_body = assert_status(login_response, "200 OK")
            if login_headers.get("cache-control") != "no-store":
                raise AssertionError("Loginseite verhindert Browser-Caching nicht")

            csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', login_body)
            if csrf_match is None:
                raise AssertionError("Loginseite enthält kein CSRF-Token")

            login_body_data = urlencode({
                "csrf_token": csrf_match.group(1).decode("ascii"),
                "username": TEST_ADMIN_USERNAME,
                "password": TEST_ADMIN_PASSWORD,
            })
            authenticated_response = raw_request(request_text(
                "POST",
                "/admin/login",
                proxy_headers(ip_address, {
                    "Content-Type": "application/x-www-form-urlencoded",
                }),
                login_body_data,
            ).encode())
            authenticated_headers, _ = assert_status(
                authenticated_response,
                "303 See Other",
            )
            if authenticated_headers.get("location") != "/admin":
                raise AssertionError("Erfolgreiche Anmeldung leitet nicht zum Dashboard")

            set_cookie = authenticated_headers.get("set-cookie", "")
            for required in (
                "styles4dogs_admin=",
                "HttpOnly",
                "SameSite=Lax",
                "Path=/admin",
                "Max-Age=",
            ):
                if required not in set_cookie:
                    raise AssertionError(f"Session-Cookie enthält {required!r} nicht")
            return set_cookie.split(";", 1)[0], set_cookie

        def load_dashboard(cookie_value: str) -> tuple[bytes, str]:
            dashboard_response = raw_request(request_text(
                "GET",
                "/admin",
                proxy_headers(client_ip, {"Cookie": cookie_value}),
            ).encode())
            _, dashboard_body = assert_status(dashboard_response, "200 OK")
            if TEST_ADMIN_USERNAME.encode() not in dashboard_body:
                raise AssertionError("Dashboard zeigt den angemeldeten Admin nicht an")
            csrf_match = re.search(
                rb'<form[^>]+action="/admin/logout"[^>]*>.*?name="csrf_token" value="([0-9a-f]+)"',
                dashboard_body,
                re.DOTALL,
            )
            if csrf_match is None:
                raise AssertionError("Dashboard enthält kein CSRF-geschütztes Logout-Formular")
            return dashboard_body, csrf_match.group(1).decode("ascii")

        cookie, _ = create_session(client_ip)
        _, session_csrf = load_dashboard(cookie)

        # Eine erneute erfolgreiche Anmeldung rotiert die serverseitige Sitzung.
        rotated_cookie, _ = create_session(client_ip)
        if rotated_cookie == cookie:
            raise AssertionError("Erneute Anmeldung rotiert das Session-Token nicht")
        old_session_response = raw_request(request_text(
            "GET",
            "/admin",
            proxy_headers(client_ip, {"Cookie": cookie}),
        ).encode())
        old_session_headers, _ = assert_status(old_session_response, "303 See Other")
        if old_session_headers.get("location") != "/admin/login":
            raise AssertionError("Rotierte Vorgängersitzung bleibt verwendbar")
        cookie = rotated_cookie
        _, session_csrf = load_dashboard(cookie)

        invalid_logout = raw_request(request_text(
            "POST",
            "/admin/logout",
            proxy_headers(client_ip, {
                "Cookie": cookie,
                "Content-Type": "application/x-www-form-urlencoded",
            }),
            urlencode({"csrf_token": "0" * len(session_csrf)}),
        ).encode())
        assert_status(invalid_logout, "403 Forbidden")

        logout_response = raw_request(request_text(
            "POST",
            "/admin/logout",
            proxy_headers(client_ip, {
                "Cookie": cookie,
                "Content-Type": "application/x-www-form-urlencoded",
            }),
            urlencode({"csrf_token": session_csrf}),
        ).encode())
        logout_headers, _ = assert_status(logout_response, "303 See Other")
        if logout_headers.get("location") != "/admin/login":
            raise AssertionError("Logout leitet nicht zur Anmeldung")
        clear_cookie = logout_headers.get("set-cookie", "")
        if "Max-Age=0" not in clear_cookie:
            raise AssertionError("Logout löscht das Session-Cookie nicht")

        logged_out_response = raw_request(request_text(
            "GET",
            "/admin",
            proxy_headers(client_ip, {"Cookie": cookie}),
        ).encode())
        logged_out_headers, _ = assert_status(logged_out_response, "303 See Other")
        if logged_out_headers.get("location") != "/admin/login":
            raise AssertionError("Ausgeloggte Session bleibt verwendbar")

        # Absolut abgelaufene Sitzungen werden auch mit formal gültigem Cookie abgewiesen.
        expired_cookie, _ = create_session(client_ip)
        database_file_value = os.environ.get("STYLES4DOGS_TEST_DATABASE_FILE")
        if not database_file_value:
            raise AssertionError("STYLES4DOGS_TEST_DATABASE_FILE fehlt")
        with sqlite3.connect(database_file_value) as database:
            database.execute(
                "UPDATE admin_sessions SET created_at=0, expires_at=1, last_activity_at=0 "
                "WHERE admin_id=(SELECT id FROM admin_users WHERE username=?);",
                (TEST_ADMIN_USERNAME,),
            )
        expired_response = raw_request(request_text(
            "GET",
            "/admin",
            proxy_headers(client_ip, {"Cookie": expired_cookie}),
        ).encode())
        expired_headers, _ = assert_status(expired_response, "303 See Other")
        if expired_headers.get("location") != "/admin/login":
            raise AssertionError("Abgelaufene Session bleibt verwendbar")

        limited_ip = "198.51.100.46"
        for attempt in range(6):
            page_response = raw_request(request_text(
                "GET",
                "/admin/login",
                proxy_headers(limited_ip),
            ).encode())
            _, page_body = assert_status(page_response, "200 OK")
            token_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', page_body)
            if token_match is None:
                raise AssertionError("Rate-Limit-Test erhält kein Login-CSRF-Token")
            response = raw_request(request_text(
                "POST",
                "/admin/login",
                proxy_headers(limited_ip, {
                    "Content-Type": "application/x-www-form-urlencoded",
                }),
                urlencode({
                    "csrf_token": token_match.group(1).decode("ascii"),
                    "username": TEST_ADMIN_USERNAME,
                    "password": "definitiv-falsch",
                }),
            ).encode())
            headers, _ = assert_status(response, "303 See Other")
            expected_location = (
                "/admin/login?error=limited"
                if attempt >= 5
                else "/admin/login?error=invalid"
            )
            if headers.get("location") != expected_location:
                raise AssertionError(
                    "Login-Rate-Limit liefert unerwartete Weiterleitung: "
                    f"{headers.get('location')!r}"
                )

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
        if row[4] != "image/png" or row[5] != len(image_bytes) or row[6:] != (0, 1):
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
    def admin_gallery_reorder_workflow() -> None:
        database_file_value = os.environ.get(
            "STYLES4DOGS_TEST_DATABASE_FILE"
        )
        if not database_file_value:
            raise AssertionError(
                "STYLES4DOGS_TEST_DATABASE_FILE fehlt"
            )

        database_file = Path(database_file_value)

        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()

        auth_headers = {
            "Authorization": f"Basic {auth_token}"
        }

        image_ids: list[int] = []

        try:
            with sqlite3.connect(database_file) as connection:
                connection.execute(
                    "DELETE FROM gallery_images "
                    "WHERE file_name LIKE 'reorder-test-%'"
                )

                titles = (
                    "Foto Eins",
                    "Foto Zwei",
                    "Foto Drei",
                )

                for index, title in enumerate(titles):
                    cursor = connection.execute(
                        "INSERT INTO gallery_images("
                        "created_at, title, alt_text, file_name, "
                        "mime_type, image_data, sort_order, visible"
                        ") VALUES(?, ?, ?, ?, 'image/png', ?, ?, 1)",
                        (
                            "2026-07-22T12:00:00Z",
                            title,
                            f"Alternativtext {index + 1}",
                            f"reorder-test-{index + 1}.png",
                            b"test-image",
                            index,
                        ),
                    )

                    image_ids.append(cursor.lastrowid)

            admin_response = raw_request(
                request_text(
                    "GET",
                    "/admin/gallery",
                    auth_headers,
                ).encode()
            )

            _, admin_body = assert_status(
                admin_response,
                "200 OK",
            )

            csrf_match = re.search(
                rb'name="csrf_token" value="([0-9a-f]+)"',
                admin_body,
            )

            if csrf_match is None:
                raise AssertionError(
                    "Galerie-Adminseite enthält kein CSRF-Token"
                )

            if (
                    b'data-gallery-move="up"' not in admin_body
                    or b'data-gallery-move="down"' not in admin_body
            ):
                raise AssertionError(
                    "Galerie-Adminseite enthält nicht beide "
                    "Sortierknöpfe"
                )

            if b"gallery-drag-handle" in admin_body:
                raise AssertionError(
                    "Der entfernte Ziehgriff wird noch ausgeliefert"
                )

            csrf_token = csrf_match.group(1).decode("ascii")

            expected_ids = [
                image_ids[2],
                image_ids[0],
                image_ids[1],
            ]

            reorder_body = urlencode({
                "csrf_token": csrf_token,
                "order": ",".join(
                    str(image_id)
                    for image_id in expected_ids
                ),
            })

            reorder_response = raw_request(
                request_text(
                    "POST",
                    "/admin/gallery/reorder",
                    {
                        **auth_headers,
                        "Content-Type":
                            "application/x-www-form-urlencoded",
                    },
                    reorder_body,
                ).encode()
            )

            _, reorder_response_body = assert_status(
                reorder_response,
                "204 No Content",
            )

            if reorder_response_body != b"":
                raise AssertionError(
                    "Galerie-Sortierung liefert trotz 204 "
                    "einen Response-Body"
                )

            with sqlite3.connect(database_file) as connection:
                persisted_ids = [
                    row[0]
                    for row in connection.execute(
                        "SELECT id FROM gallery_images "
                        "WHERE id IN (?, ?, ?) "
                        "ORDER BY sort_order ASC",
                        tuple(image_ids),
                    )
                ]

            if persisted_ids != expected_ids:
                raise AssertionError(
                    "Galerie-Reihenfolge wurde nicht gespeichert: "
                    f"{persisted_ids!r}"
                )

            api_response = raw_request(
                request_text(
                    "GET",
                    "/api/gallery",
                ).encode()
            )

            _, api_body = assert_status(
                api_response,
                "200 OK",
            )

            api_items = json.loads(api_body)
            api_ids = [
                item["id"]
                for item in api_items
            ]

            if api_ids != expected_ids:
                raise AssertionError(
                    "Galerie-API liefert die falsche Reihenfolge: "
                    f"{api_ids!r}"
                )

        finally:
            if image_ids:
                with sqlite3.connect(database_file) as connection:
                    connection.execute(
                        "DELETE FROM gallery_images "
                        "WHERE id IN (?, ?, ?)",
                        tuple(image_ids),
                    )
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
        if ("Teststraße 12a".encode("utf-8") not in admin_body or
            b"26121 Oldenburg" not in admin_body):
            raise AssertionError("Adminbereich zeigt die gespeicherte Pflichtadresse nicht")
        for group in ("Neue Anfragen", "Bestätigt", "Abgelehnt", "Abgesagt", "Erledigt"):
            if group.encode("utf-8") not in admin_body:
                raise AssertionError(f"Statussegment fehlt: {group!r}")
        for removed_group in ("Kontaktiert", "Altbestand"):
            if removed_group.encode("utf-8") in admin_body:
                raise AssertionError(f"Entferntes Statussegment wird noch angezeigt: {removed_group!r}")
        for value, label in (
            ("neu", "Neu"),
            ("bestätigt", "Bestätigt"),
            ("abgelehnt", "Abgelehnt"),
            ("abgesagt", "Abgesagt"),
            ("erledigt", "Erledigt"),
        ):
            option = f'<option value="{value}">{label}</option>'.encode("utf-8")
            selected = f'<option value="{value}" selected>{label}</option>'.encode("utf-8")
            if option not in admin_body and selected not in admin_body:
                raise AssertionError(f"Statusoption fehlt: {label!r}")
        if b'class="booking-status-group booking-status-group-abgelehnt" open' in admin_body:
            raise AssertionError("Abgelehnte Buchungen sind nicht standardmäßig eingeklappt")
        csrf_token = csrf_match.group(1).decode("ascii")

        with sqlite3.connect(database_file) as connection:
            target = connection.execute(
                "SELECT id, status FROM bookings WHERE customer_name = ?",
                ("Legacy Test",),
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
            "status": "abgelehnt",
        })
        missing_auth_response = raw_request(request_text(
            "POST",
            "/admin/bookings/status",
            {"Content-Type": "application/x-www-form-urlencoded"},
            missing_auth_body,
        ).encode())
        assert_status(missing_auth_response, "303 See Other")

        invalid_csrf_body = urlencode({
            "csrf_token": "0" * 64,
            "booking_id": str(target_id),
            "status": "abgelehnt",
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
            "status": "abgelehnt",
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
            "status": "abgelehnt",
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
            "status": "abgelehnt",
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
            updated_status, updated_decision_status = connection.execute(
                "SELECT status, decision_status FROM bookings WHERE id = ?",
                (target_id,),
            ).fetchone()
            current_untouched_status = connection.execute(
                "SELECT status FROM bookings WHERE id = ?",
                (untouched_id,),
            ).fetchone()[0]

        if updated_status != "abgelehnt":
            raise AssertionError(f"Status wurde nicht gespeichert: {updated_status!r}")
        if updated_decision_status != "rejected":
            raise AssertionError(
                "Manueller Status und Terminentscheidung wurden nicht synchronisiert"
            )
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
            '<option value="abgelehnt" selected>Abgelehnt</option>'
            .encode("utf-8")
        )
        if selected_pattern not in refreshed_body:
            raise AssertionError("Adminseite zeigt den gespeicherten Status nicht als ausgewählt")

    def admin_filter_workflow() -> None:
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}

        status_query = urlencode({"status": "abgelehnt"})
        status_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{status_query}",
            auth_headers,
        ).encode())
        _, status_body = assert_status(status_response, "200 OK")

        if b"Legacy Test" not in status_body:
            raise AssertionError("Statusfilter zeigt die abgelehnte Buchung nicht")
        if b"TSV V2 Test" in status_body or b"Pew Pew Test" in status_body:
            raise AssertionError("Statusfilter zeigt Buchungen mit anderem Status")
        if '<option value="abgelehnt" selected>'.encode("utf-8") not in status_body:
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

        address_query = urlencode({"q": "26121"})
        address_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{address_query}",
            auth_headers,
        ).encode())
        _, address_body = assert_status(address_response, "200 OK")
        if b"Pew Pew Test" not in address_body or b"Legacy Test" in address_body:
            raise AssertionError("Adresssuche filtert die Buchungen nicht korrekt")

        literal_wildcard_query = urlencode({"q": "%"})
        literal_wildcard_response = raw_request(request_text(
            "GET",
            f"/admin/bookings?{literal_wildcard_query}",
            auth_headers,
        ).encode())
        _, literal_wildcard_body = assert_status(literal_wildcard_response, "200 OK")

        if "Für diese Suche wurden keine Buchungsanfragen gefunden.".encode("utf-8") not in literal_wildcard_body:
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


    def admin_dashboard_overview() -> None:
        valid_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        unauthorized = raw_request(request_text("GET", "/admin").encode())
        assert_status(unauthorized, "303 See Other")

        response = raw_request(request_text(
            "GET",
            "/admin",
            {"Authorization": f"Basic {valid_token}"},
        ).encode())
        _, body = assert_status(response, "200 OK")
        if "Salonübersicht".encode("utf-8") not in body or "Fehlgeschlagene E-Mails".encode("utf-8") not in body:
            raise AssertionError("Admin-Dashboard enthält nicht die erwarteten Kennzahlen")
        if b"/admin/appointments" not in body or b"/admin/gallery" not in body:
            raise AssertionError("Admin-Dashboard enthält nicht die erwarteten Schnellzugriffe")


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
                "SELECT decision_status, status, hold_expires_at, decision_at "
                "FROM bookings WHERE id = ?",
                (pending_id,),
            ).fetchone()
        if accepted is None or accepted[0:2] != ("confirmed", "bestätigt") or accepted[2] is not None or not accepted[3]:
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
            **address_fields("Mobilweg 7", "26122", "Oldenburg"),
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
                "SELECT decision_status, status, hold_expires_at, rejection_reason "
                "FROM bookings WHERE id = ?",
                (phone_booking[0],),
            ).fetchone()
        if rejected != ("rejected", "abgelehnt", None, "Der Termin ist intern nicht möglich"):
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
            'id="oeffnungszeiten"',
            "Leistungen und Dauer",
            "Urlaub und Sperrzeiten",
            "Leistung hinzufügen",
            'name="auto_confirm_bookings"',
            "Frühester buchbarer Termin",
            'name="pending_hold_hours"',
            "Änderungen speichern",
            "/admin/calendar/save-all",
            "/admin-calendar.js",
            "opening-period-grid-three",
            "opening-period-optional",
            "calendar-save-floating",
            "Buchungsschutz",
        ):
            if expected.encode("utf-8") not in page_body:
                raise AssertionError(f"Admin-Kalender enthält {expected!r} nicht")

        page_text = page_body.decode("utf-8")
        closures_position = page_text.find('id="sperrzeiten"')
        save_position = page_text.find('id="speichern"')
        protection_position = page_text.find('id="buchungsschutz"')
        if not (0 <= closures_position < save_position < protection_position):
            raise AssertionError(
                "Reihenfolge aus Sperrzeiten, Änderungen übernehmen und Buchungsschutz ist falsch"
            )
        save_button_match = re.search(
            r'<button id="calendar-save-bottom"[^>]*disabled',
            page_text,
        )
        if save_button_match is None:
            raise AssertionError("Unterer Speicherknopf startet nicht deaktiviert")

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
                "cancellation_notice_hours": "72",
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
                "cancellation_notice_hours": "72",
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
            "cancellation_notice_hours": "72",
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
                "email_notifications_enabled, reminder_enabled, reminder_lead_minutes, "
                "cancellation_notice_minutes "
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

        if settings != (0, 120, 30, 720, 0, 0, 1, 1440, 4320):
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
                **address_fields(f"Spamweg {index + 1}", "26123", "Oldenburg"),
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
            "E-Mail-System",
            "E-Mail-Konto verbinden",
            "Automatische Nachrichten individualisieren",
            "Terminbestätigung",
            "Terminabsage",
            "Kundenabsage für den Admin",
            "{{customer_first_name}}",
            "{{customer_last_name}}",
            "Vornamen",
            "Nachnamen des Kunden",
        ):
            if expected.encode("utf-8") not in body:
                raise AssertionError(f"E-Mail-Adminseite enthält {expected!r} nicht")

        if body.count(b'<details class="notification-template-card"') != 8:
            raise AssertionError("Nicht jede E-Mail-Vorlage ist einklappbar")
        if body.count(b'class="notification-template-summary"') != 8:
            raise AssertionError("Einklappbare E-Mail-Vorlagen haben keine Zusammenfassung")

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

        pause_response = raw_request(request_text(
            "POST",
            "/admin/notifications/toggle",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "delivery_enabled": "0",
            }),
        ).encode())
        pause_headers, _ = assert_status(pause_response, "303 See Other")
        if pause_headers.get("location") != "/admin/notifications?saved=system":
            raise AssertionError("Pausieren des E-Mail-Systems leitet falsch zurück")

        paused_page = raw_request(request_text(
            "GET", "/admin/notifications", auth_headers
        ).encode())
        _, paused_body = assert_status(paused_page, "200 OK")
        if "Automatischer Versand ist pausiert".encode("utf-8") not in paused_body:
            raise AssertionError("E-Mail-Hauptschalter zeigt den pausierten Zustand nicht")

        enable_response = raw_request(request_text(
            "POST",
            "/admin/notifications/toggle",
            form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "delivery_enabled": "1",
            }),
        ).encode())
        assert_status(enable_response, "303 See Other")

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
                "cancellation_notice_hours": "72",
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
            **address_fields("Automatikstraße 8", "26124", "Oldenburg"),
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

        portal_match = re.search(
            rb'href="[^" ]*(/buchung/[0-9]+/[0-9a-f]{64})"',
            confirmation_body,
        )
        if portal_match is None:
            raise AssertionError("Automatische Bestätigung enthält keinen persönlichen Kundenlink")

        portal_response = raw_request(request_text(
            "GET",
            portal_match.group(1).decode("ascii"),
        ).encode())
        _, portal_body = assert_status(portal_response, "200 OK")
        if (
            "innerhalb von 72 Stunden".encode("utf-8") not in portal_body
            or "Ausfallgebühr".encode("utf-8") not in portal_body
        ):
            raise AssertionError("Kundenbereich zeigt den Stornierungshinweis nicht an")

        with sqlite3.connect(database_file) as connection:
            row = connection.execute(
                "SELECT id, decision_status, status, hold_expires_at, decision_at, contact_channel, email "
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

        if row[1:3] != ("confirmed", "bestätigt") or row[3] is not None or not row[4] or row[5:] != (
            "email", "auto@example.invalid"
        ):
            raise AssertionError(f"Automatisch bestätigter Termin wurde falsch gespeichert: {row!r}")
        if notification is None or notification[0] != "pending" or notification[1] != "auto@example.invalid":
            raise AssertionError(f"Bestätigungs-E-Mail wurde nicht eingereiht: {notification!r}")
        expected_date = datetime.strptime(target_date, "%Y-%m-%d").strftime("%d.%m.%Y")
        if (
            "Termin bestätigt" not in notification[2]
            or "Hallo Auto," not in notification[3]
            or expected_date not in notification[3]
            or selected_slot["start"] not in notification[3]
            or "Milo" not in notification[3]
            or portal_match.group(1).decode("ascii") not in notification[3]
        ):
            raise AssertionError(
                "Bestätigungs-E-Mail enthält nicht die erwarteten Termindaten"
            )
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
                "cancellation_notice_hours": "72",
                "reminder_lead_hours": "168",
                "email_notifications_enabled": "1",
                "reminder_enabled": "1",
            }),
        ).encode())
        assert_status(disable_response, "303 See Other")


    def customer_portal_cancellation() -> None:
        database_file = Path(os.environ["STYLES4DOGS_TEST_DATABASE_FILE"])
        cancellation_reason = "Unser Urlaub wurde kurzfristig verschoben"
        with sqlite3.connect(database_file) as connection:
            connection.execute(
                "UPDATE calendar_settings SET cancellation_notice_minutes = ? WHERE id = 1",
                (30 * 24 * 60,),
            )
            connection.commit()
        target = datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=21)
        target_date = target.date().isoformat()
        query = urlencode({
            "service": "claw_care",
            "from": target_date,
            "to": target_date,
        })

        availability_response = raw_request(request_text(
            "GET",
            f"/api/availability?{query}",
        ).encode())
        _, availability_body = assert_status(availability_response, "200 OK")
        availability = json.loads(availability_body)
        free_slots = [slot for slot in availability["days"][0]["slots"] if slot["available"]]
        if not free_slots:
            raise AssertionError("Kein freier Termin für den Kundenbereich-Absagetest vorhanden")
        selected_slot = free_slots[0]

        booking_body = urlencode({
            "first_name": "Portal",
            "last_name": "Absage",
            **email_contact("portal-absage@example.invalid"),
            **address_fields("Portalweg 9", "26125", "Oldenburg"),
            "dog_name": "Flocke",
            "dog_size": "small",
            "service": "claw_care",
            "appointment_date": target_date,
            "appointment_start": selected_slot["start"],
            "message": "Kundenbereich-Absagetest",
            "privacy_consent": "accepted",
        })
        booking_response = raw_request(request_text(
            "POST",
            "/booking",
            proxy_headers(
                "198.51.100.190",
                {"Content-Type": "application/x-www-form-urlencoded"},
            ),
            booking_body,
        ).encode())
        _, booking_page = assert_status(booking_response, "201 Created")
        portal_match = re.search(rb'href="[^" ]*(/buchung/([0-9]+)/[0-9a-f]{64})"', booking_page)
        if portal_match is None:
            raise AssertionError("Absagetest erhielt keinen persönlichen Kundenlink")
        portal_path = portal_match.group(1).decode("ascii")
        booking_id = int(portal_match.group(2))

        cancel_response = raw_request(request_text(
            "POST",
            f"{portal_path}/cancel",
            {"Content-Type": "application/x-www-form-urlencoded"},
            urlencode({
                "confirm_cancel": "1",
                "cancellation_reason": cancellation_reason,
            }),
        ).encode())
        cancel_headers, _ = assert_status(cancel_response, "303 See Other")
        expected_location = f"{portal_path}?cancelled=1"
        if cancel_headers.get("location") != expected_location:
            raise AssertionError("Kundenabsage leitet nicht korrekt zurück")

        cancelled_page_response = raw_request(request_text(
            "GET",
            expected_location,
        ).encode())
        _, cancelled_page = assert_status(cancelled_page_response, "200 OK")
        if (
            "Von dir abgesagt".encode("utf-8") not in cancelled_page
            or "wieder freigegeben".encode("utf-8") not in cancelled_page
            or cancellation_reason.encode("utf-8") not in cancelled_page
        ):
            raise AssertionError("Kundenbereich bestätigt die Absage nicht verständlich")

        repeated_cancel = raw_request(request_text(
            "POST",
            f"{portal_path}/cancel",
            {"Content-Type": "application/x-www-form-urlencoded"},
            urlencode({"confirm_cancel": "1"}),
        ).encode())
        _, repeated_body = assert_status(repeated_cancel, "200 OK")
        if "Von dir abgesagt".encode("utf-8") not in repeated_body:
            raise AssertionError("Wiederholte Absage zeigt nicht den aktuellen Status")

        with sqlite3.connect(database_file) as connection:
            row = connection.execute(
                "SELECT decision_status, status, hold_expires_at, decision_at, cancelled_at, "
                "       cancellation_reason, cancellation_actor, late_cancellation "
                "FROM bookings WHERE id = ?",
                (booking_id,),
            ).fetchone()
            admin_cancellation = connection.execute(
                "SELECT recipient_email, status, subject, body_text "
                "FROM notification_jobs "
                "WHERE booking_id = ? AND event_type = 'admin_booking_cancelled'",
                (booking_id,),
            ).fetchone()
            customer_cancellation = connection.execute(
                "SELECT recipient_email, status, subject, body_text "
                "FROM notification_jobs "
                "WHERE booking_id = ? AND event_type = 'booking_cancelled'",
                (booking_id,),
            ).fetchone()
            cancellation_event = connection.execute(
                "SELECT event_type, actor_type, reason FROM booking_events "
                "WHERE booking_id = ? AND event_type = 'customer_cancelled' "
                "ORDER BY id DESC LIMIT 1",
                (booking_id,),
            ).fetchone()
            duplicate_jobs = connection.execute(
                "SELECT event_type, COUNT(*) FROM notification_jobs "
                "WHERE booking_id = ? AND event_type IN ('booking_cancelled','admin_booking_cancelled') "
                "GROUP BY event_type ORDER BY event_type",
                (booking_id,),
            ).fetchall()
            connection.execute(
                "UPDATE calendar_settings SET cancellation_notice_minutes = 4320 WHERE id = 1"
            )
            connection.commit()
        if (
            row is None
            or row[0:2] != ("cancelled", "abgesagt")
            or row[2] is not None
            or not row[3]
            or not row[4]
            or row[5:] != (cancellation_reason, "customer", 1)
        ):
            raise AssertionError(f"Kundenabsage wurde falsch gespeichert: {row!r}")
        if admin_cancellation is None or admin_cancellation[0:2] != (
            "admin@example.invalid", "pending"
        ):
            raise AssertionError(
                f"Admin-Mail zur Kundenabsage wurde nicht eingereiht: {admin_cancellation!r}"
            )
        if (
            "Kundenabsage" not in admin_cancellation[2]
            or "Portal Absage" not in admin_cancellation[3]
            or "Flocke" not in admin_cancellation[3]
            or str(booking_id) not in admin_cancellation[3]
            or cancellation_reason not in admin_cancellation[3]
            or "kurzfristig" not in admin_cancellation[3].lower()
        ):
            raise AssertionError(
                "Admin-Mail zur Kundenabsage enthält nicht die erwarteten Buchungsdaten"
            )
        if customer_cancellation is None or customer_cancellation[0:2] != (
            "portal-absage@example.invalid", "pending"
        ):
            raise AssertionError(
                f"Kundenmail zur Absage wurde nicht eingereiht: {customer_cancellation!r}"
            )
        if (
            "Terminabsage" not in customer_cancellation[2]
            or cancellation_reason not in customer_cancellation[3]
            or "kurzfristig" not in customer_cancellation[3].lower()
        ):
            raise AssertionError("Absagebestätigung enthält Grund oder Fristenhinweis nicht")
        if cancellation_event != ("customer_cancelled", "customer", cancellation_reason):
            raise AssertionError(f"Kundenabsage wurde nicht korrekt protokolliert: {cancellation_event!r}")
        if duplicate_jobs != [("admin_booking_cancelled", 1), ("booking_cancelled", 1)]:
            raise AssertionError(f"Kundenabsage erzeugte doppelte Nachrichten: {duplicate_jobs!r}")

        availability_after = raw_request(request_text(
            "GET",
            f"/api/availability?{query}",
        ).encode())
        _, availability_after_body = assert_status(availability_after, "200 OK")
        after_payload = json.loads(availability_after_body)
        slot_after = next(
            slot for slot in after_payload["days"][0]["slots"]
            if slot["start"] == selected_slot["start"]
        )
        if not slot_after["available"]:
            raise AssertionError("Vom Kunden abgesagter Zeitraum wurde nicht wieder freigegeben")

    def phase14_booking_management() -> None:
        database_file = Path(os.environ["STYLES4DOGS_TEST_DATABASE_FILE"])
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {
            **auth_headers,
            "Content-Type": "application/x-www-form-urlencoded",
        }

        admin_page_response = raw_request(request_text(
            "GET", "/admin/bookings", auth_headers,
        ).encode())
        _, admin_page = assert_status(admin_page_response, "200 OK")
        csrf_match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', admin_page)
        if csrf_match is None:
            raise AssertionError("Phase-14-Test erhält kein Admin-CSRF-Token")
        csrf_token = csrf_match.group(1).decode("ascii")

        def free_slot(service: str, first_day: int) -> tuple[str, str]:
            for offset in range(first_day, first_day + 28):
                date = (
                    datetime.now(ZoneInfo("Europe/Berlin")) + timedelta(days=offset)
                ).date().isoformat()
                query = urlencode({"service": service, "from": date, "to": date})
                response = raw_request(request_text(
                    "GET", f"/api/availability?{query}",
                ).encode())
                _, body = assert_status(response, "200 OK")
                payload = json.loads(body)
                slots = [slot for slot in payload["days"][0]["slots"] if slot["available"]]
                if slots:
                    return date, slots[0]["start"]
            raise AssertionError("Kein freier Termin für den Phase-14-Test vorhanden")

        def create_booking(
            first_name: str,
            last_name: str,
            email: str,
            dog_name: str,
            date: str,
            start: str,
            client_ip: str,
        ) -> tuple[int, str]:
            body = urlencode({
                "first_name": first_name,
                "last_name": last_name,
                **email_contact(email),
                "street_address": "Historienweg 14",
                "postal_code": "26123",
                "city": "Oldenburg",
                "dog_name": dog_name,
                "dog_size": "small",
                "service": "claw_care",
                "appointment_date": date,
                "appointment_start": start,
                "message": "Phase-14-Verwaltungstest",
                "privacy_consent": "accepted",
            })
            response = raw_request(request_text(
                "POST",
                "/booking",
                proxy_headers(client_ip, {
                    "Content-Type": "application/x-www-form-urlencoded",
                }),
                body,
            ).encode())
            _, response_body = assert_status(response, "201 Created")
            match = re.search(rb'href="[^" ]*(/buchung/([0-9]+)/[0-9a-f]{64})"', response_body)
            if match is None:
                raise AssertionError("Phase-14-Buchung enthält keinen Kundenlink")
            return int(match.group(2)), match.group(1).decode("ascii")

        def accept_booking(booking_id: int) -> None:
            response = raw_request(request_text(
                "POST",
                "/admin/bookings/accept",
                form_headers,
                urlencode({
                    "csrf_token": csrf_token,
                    "booking_id": str(booking_id),
                }),
            ).encode())
            assert_status(response, "303 See Other")

        original_date, original_start = free_slot("claw_care", 24)
        booking_id, portal_path = create_booking(
            "Phase", "Vierzehn", "phase14@example.invalid", "Momo",
            original_date, original_start, "198.51.100.210",
        )
        accept_booking(booking_id)

        target_date, target_start = free_slot("claw_care", 35)
        edit_response = raw_request(request_text(
            "GET", f"/admin/bookings/edit?id={booking_id}", auth_headers,
        ).encode())
        _, edit_page = assert_status(edit_response, "200 OK")
        for expected in (
            b"Buchungsdaten",
            b"Interne Hundenotiz",
            b"Kunden- und Hundehistorie",
            b'action="/admin/bookings/update"',
        ):
            if expected not in edit_page:
                raise AssertionError(f"Buchungsverwaltung enthält {expected!r} nicht")

        update_payload = {
            "csrf_token": csrf_token,
            "booking_id": str(booking_id),
            "first_name": "Phase",
            "last_name": "Vierzehn",
            "email": "phase14@example.invalid",
            "phone_number": "",
            "phone_kind": "",
            "contact_channel": "email",
            "contact_preference": "",
            "street_address": "Historienweg 14",
            "postal_code": "26123",
            "city": "Oldenburg",
            "dog_name": "Momo",
            "dog_breed": "",
            "dog_size": "small",
            "service_code": "claw_care",
            "appointment_date": target_date,
            "appointment_start": target_start,
            "message": "Phase-14-Verwaltungstest",
            "admin_note": "Nur intern sichtbar",
        }
        update_response = raw_request(request_text(
            "POST", "/admin/bookings/update", form_headers,
            urlencode(update_payload),
        ).encode())
        update_headers, _ = assert_status(update_response, "303 See Other")
        expected_redirect = f"/admin/bookings/edit?id={booking_id}&saved=rescheduled"
        if update_headers.get("location") != expected_redirect:
            raise AssertionError("Terminverschiebung leitet nicht korrekt zurück")

        with sqlite3.connect(database_file) as connection:
            updated = connection.execute(
                "SELECT appointment_date,start_minute,status,decision_status,admin_note,"
                "       customer_id,dog_id,dog_breed FROM bookings WHERE id=?",
                (booking_id,),
            ).fetchone()
            reschedule_jobs = connection.execute(
                "SELECT subject,body_text,ics_content FROM notification_jobs "
                "WHERE booking_id=? AND event_type='booking_rescheduled'",
                (booking_id,),
            ).fetchall()
            confirmation_ics = connection.execute(
                "SELECT ics_content FROM notification_jobs "
                "WHERE booking_id=? AND event_type='booking_confirmed' ORDER BY id LIMIT 1",
                (booking_id,),
            ).fetchone()
            reschedule_events = connection.execute(
                "SELECT old_value,new_value,actor_type FROM booking_events "
                "WHERE booking_id=? AND event_type='booking_rescheduled'",
                (booking_id,),
            ).fetchall()
        target_minute = int(target_start[:2]) * 60 + int(target_start[3:])
        if (
            updated is None
            or updated[0:5] != (
                target_date, target_minute, "bestätigt", "confirmed", "Nur intern sichtbar"
            )
            or updated[5] <= 0
            or updated[6] <= 0
            or updated[7] != ""
        ):
            raise AssertionError(f"Terminverschiebung wurde falsch gespeichert: {updated!r}")
        if len(reschedule_jobs) != 1 or len(reschedule_events) != 1:
            raise AssertionError(
                f"Terminverschiebung erzeugte falsche Job-/Eventzahl: "
                f"{len(reschedule_jobs)}/{len(reschedule_events)}"
            )
        original_date_display = datetime.strptime(original_date, "%Y-%m-%d").strftime("%d.%m.%Y")
        target_date_display = datetime.strptime(target_date, "%Y-%m-%d").strftime("%d.%m.%Y")
        if (
            original_date_display not in reschedule_jobs[0][1]
            or target_date_display not in reschedule_jobs[0][1]
            or portal_path not in reschedule_jobs[0][1]
        ):
            raise AssertionError("Verschiebungsmail enthält alte, neue oder Portal-Daten nicht")
        uid_pattern = re.compile(r"^UID:([^\r\n]+)", re.MULTILINE)
        old_uid = uid_pattern.search(confirmation_ics[0] if confirmation_ics else "")
        new_uid = uid_pattern.search(reschedule_jobs[0][2])
        if old_uid is None or new_uid is None or old_uid.group(1) != new_uid.group(1):
            raise AssertionError("Aktualisierte ICS-Datei verwendet keine stabile Termin-UID")
        if reschedule_events[0][2] != "admin":
            raise AssertionError("Terminverschiebung wurde nicht als Adminaktion protokolliert")

        repeated_response = raw_request(request_text(
            "POST", "/admin/bookings/update", form_headers,
            urlencode(update_payload),
        ).encode())
        repeated_headers, _ = assert_status(repeated_response, "303 See Other")
        if repeated_headers.get("location") != f"/admin/bookings/edit?id={booking_id}&saved=updated":
            raise AssertionError("Idempotente Buchungsänderung wird falsch behandelt")
        with sqlite3.connect(database_file) as connection:
            repeated_counts = connection.execute(
                "SELECT "
                "(SELECT COUNT(*) FROM notification_jobs WHERE booking_id=? AND event_type='booking_rescheduled'),"
                "(SELECT COUNT(*) FROM booking_events WHERE booking_id=? AND event_type='booking_rescheduled')",
                (booking_id, booking_id),
            ).fetchone()
        if repeated_counts != (1, 1):
            raise AssertionError(f"Wiederholung erzeugte doppelte Verschiebungen: {repeated_counts!r}")

        update_payload["city"] = "Oldenburg-Nord"
        contact_update = raw_request(request_text(
            "POST", "/admin/bookings/update", form_headers,
            urlencode(update_payload),
        ).encode())
        contact_headers, _ = assert_status(contact_update, "303 See Other")
        if contact_headers.get("location") != f"/admin/bookings/edit?id={booking_id}&saved=updated":
            raise AssertionError("Reine Stammdatenänderung wird als Verschiebung behandelt")

        with sqlite3.connect(database_file) as connection:
            dog_id = connection.execute(
                "SELECT dog_id FROM bookings WHERE id=?", (booking_id,),
            ).fetchone()[0]
        note_response = raw_request(request_text(
            "POST", "/admin/bookings/dog-note", form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "booking_id": str(booking_id),
                "dog_id": str(dog_id),
                "dog_note": "Mag den Föhn nicht",
            }),
        ).encode())
        assert_status(note_response, "303 See Other")
        public_portal = raw_request(request_text("GET", portal_path).encode())
        _, public_portal_body = assert_status(public_portal, "200 OK")
        if b"Mag den F" in public_portal_body or b"Nur intern sichtbar" in public_portal_body:
            raise AssertionError("Interne Notizen erscheinen im Kundenportal")

        second_date, second_start = free_slot("claw_care", 46)
        second_id, _ = create_booking(
            "Konflikt", "Test", "konflikt@example.invalid", "Luna",
            second_date, second_start, "198.51.100.211",
        )
        accept_booking(second_id)
        conflict_payload = dict(update_payload)
        conflict_payload.update({
            "booking_id": str(second_id),
            "first_name": "Konflikt",
            "last_name": "Test",
            "email": "konflikt@example.invalid",
            "city": "Oldenburg",
            "dog_name": "Luna",
            "appointment_date": target_date,
            "appointment_start": target_start,
            "admin_note": "",
        })
        conflict_response = raw_request(request_text(
            "POST", "/admin/bookings/update", form_headers,
            urlencode(conflict_payload),
        ).encode())
        conflict_headers, _ = assert_status(conflict_response, "303 See Other")
        if conflict_headers.get("location") != f"/admin/bookings/edit?id={second_id}&saved=conflict":
            raise AssertionError("Terminkollision wird nicht verständlich zurückgemeldet")
        with sqlite3.connect(database_file) as connection:
            unchanged = connection.execute(
                "SELECT appointment_date,start_minute FROM bookings WHERE id=?", (second_id,),
            ).fetchone()
        second_minute = int(second_start[:2]) * 60 + int(second_start[3:])
        if unchanged != (second_date, second_minute):
            raise AssertionError("Konflikt hat eine Teiländerung in der Datenbank hinterlassen")

        history_date, history_start = free_slot("claw_care", 57)
        history_id, _ = create_booking(
            "Phase", "Vierzehn", "PHASE14@example.invalid", "Momo",
            history_date, history_start, "198.51.100.212",
        )
        with sqlite3.connect(database_file) as connection:
            identities = connection.execute(
                "SELECT customer_id,dog_id FROM bookings WHERE id IN (?,?) ORDER BY id",
                (booking_id, history_id),
            ).fetchall()
        if len(identities) != 2 or identities[0] != identities[1]:
            raise AssertionError(f"Normalisierte E-Mail/Hund wurden falsch verknüpft: {identities!r}")

        def create_phone_history_booking(
            number: str,
            date: str,
            start: str,
            client_ip: str,
        ) -> int:
            body = urlencode({
                "first_name": "Telefon",
                "last_name": "Historie",
                **phone_contact(number, "mobile", "call"),
                "street_address": "Telefonweg 14",
                "postal_code": "26123",
                "city": "Oldenburg",
                "dog_name": "Rufus",
                "dog_size": "medium",
                "service": "claw_care",
                "appointment_date": date,
                "appointment_start": start,
                "message": "Telefonische Historienzuordnung",
                "privacy_consent": "accepted",
            })
            response = raw_request(request_text(
                "POST",
                "/booking",
                proxy_headers(client_ip, {
                    "Content-Type": "application/x-www-form-urlencoded",
                }),
                body,
            ).encode())
            _, response_body = assert_status(response, "201 Created")
            match = re.search(rb'/buchung/([0-9]+)/[0-9a-f]{64}', response_body)
            if match is None:
                raise AssertionError("Telefonische Historienbuchung enthält keinen Kundenlink")
            return int(match.group(1))

        phone_date_one, phone_start_one = free_slot("claw_care", 68)
        phone_id_one = create_phone_history_booking(
            "+49 170 5550123", phone_date_one, phone_start_one, "198.51.100.213",
        )
        phone_date_two, phone_start_two = free_slot("claw_care", 79)
        phone_id_two = create_phone_history_booking(
            "+49-170-5550123", phone_date_two, phone_start_two, "198.51.100.214",
        )
        with sqlite3.connect(database_file) as connection:
            phone_identities = connection.execute(
                "SELECT customer_id,dog_id FROM bookings WHERE id IN (?,?) ORDER BY id",
                (phone_id_one, phone_id_two),
            ).fetchall()
        if len(phone_identities) != 2 or phone_identities[0] != phone_identities[1]:
            raise AssertionError(
                f"Normalisierte Telefonnummer/Hund wurden falsch verknüpft: {phone_identities!r}"
            )

        no_show_response = raw_request(request_text(
            "POST", "/admin/bookings/no-show", form_headers,
            urlencode({
                "csrf_token": csrf_token,
                "booking_id": str(booking_id),
                "note": "Nicht zum Termin erschienen",
            }),
        ).encode())
        no_show_headers, _ = assert_status(no_show_response, "303 See Other")
        if no_show_headers.get("location") != f"/admin/bookings/edit?id={booking_id}&saved=no-show":
            raise AssertionError("No-Show-Aktion leitet nicht korrekt zurück")
        with sqlite3.connect(database_file) as connection:
            no_show = connection.execute(
                "SELECT status,decision_status,admin_note FROM bookings WHERE id=?", (booking_id,),
            ).fetchone()
            no_show_event = connection.execute(
                "SELECT event_type,actor_type FROM booking_events "
                "WHERE booking_id=? AND event_type='booking_no_show' ORDER BY id DESC LIMIT 1",
                (booking_id,),
            ).fetchone()
            no_show_mail_count = connection.execute(
                "SELECT COUNT(*) FROM notification_jobs WHERE booking_id=? "
                "AND event_type NOT IN ('booking_received','admin_new_booking','booking_confirmed','booking_rescheduled')",
                (booking_id,),
            ).fetchone()[0]
        if no_show != ("nicht_erschienen", "no_show", "Nicht zum Termin erschienen"):
            raise AssertionError(f"No-Show-Status wurde falsch gespeichert: {no_show!r}")
        if no_show_event != ("booking_no_show", "admin") or no_show_mail_count != 0:
            raise AssertionError("No-Show wurde nicht korrekt protokolliert oder erzeugte eine Mail")

        final_page_response = raw_request(request_text(
            "GET", f"/admin/bookings/edit?id={booking_id}", auth_headers,
        ).encode())
        _, final_page = assert_status(final_page_response, "200 OK")
        for expected in (
            "Termin wurde verschoben",
            "Buchungsdaten wurden geändert",
            "Termin wurde als nicht erschienen markiert",
            "Mag den Föhn nicht",
            f"#{history_id}",
        ):
            if expected.encode("utf-8") not in final_page:
                raise AssertionError(f"Verlauf oder Historie enthält {expected!r} nicht")


    def admin_message_templates() -> None:
        database_file = Path(os.environ["STYLES4DOGS_TEST_DATABASE_FILE"])
        auth_token = base64.b64encode(
            f"{TEST_ADMIN_USERNAME}:{TEST_ADMIN_PASSWORD}".encode()
        ).decode()
        auth_headers = {"Authorization": f"Basic {auth_token}"}
        form_headers = {**auth_headers, "Content-Type": "application/x-www-form-urlencoded"}

        response = raw_request(request_text("GET", "/admin/notifications", auth_headers).encode())
        _, body = assert_status(response, "200 OK")
        for expected in (
            b"/admin-notifications.js",
            b"Vordefinierte Textbausteine",
            b"Abwesenheitsnotiz einsetzen",
            b'data-notification-template="booking_received"',
        ):
            if expected not in body:
                raise AssertionError(
                    f"E-Mail-Adminseite enthält {expected!r} nicht"
                )
        match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', body)
        if match is None:
            raise AssertionError("CSRF-Token für Vorlagen fehlt")
        csrf_token = match.group(1).decode("ascii")

        with sqlite3.connect(database_file) as connection:
            booking_link_templates = connection.execute(
                "SELECT event_type, body_template FROM notification_templates "
                "WHERE event_type IN ('booking_received', 'booking_confirmed') "
                "ORDER BY event_type"
            ).fetchall()
        if len(booking_link_templates) != 2 or any(
            "{{booking_url}}" not in template_body
            for _, template_body in booking_link_templates
        ):
            raise AssertionError(
                f"Reservierungs- oder Bestätigungsvorlage enthält keinen Buchungslink: "
                f"{booking_link_templates!r}"
            )

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
        week_page = week_body.decode("utf-8")
        if "Woche" not in week_page:
            raise AssertionError("Wochenansicht wurde nicht ausgeliefert")

        if "appointment-week-day-collapsible" in page:
            raise AssertionError("Tagesansicht klappt Termine unerwartet ein")

        with sqlite3.connect(database_file) as connection:
            multiple_appointments = connection.execute(
                "SELECT appointment_date, COUNT(*) "
                "FROM bookings "
                "WHERE decision_status IN ('pending', 'confirmed') "
                "GROUP BY appointment_date "
                "HAVING COUNT(*) > 1 "
                "ORDER BY COUNT(*) DESC, appointment_date "
                "LIMIT 1"
            ).fetchone()

        if multiple_appointments is None:
            raise AssertionError(
                "Kein Kalendertag mit mehreren Terminen für den Wochenansichtstest vorhanden"
            )

        collapsed_week_response = raw_request(request_text(
            "GET",
            f"/admin/appointments?view=week&date={multiple_appointments[0]}",
            auth_headers,
        ).encode())
        _, collapsed_week_body = assert_status(
            collapsed_week_response,
            "200 OK",
        )
        collapsed_week_page = collapsed_week_body.decode("utf-8")
        expected_count_text = f"{multiple_appointments[1]} Termine vorhanden!"

        for expected in (
            "appointment-week-day-collapsible",
            "appointment-week-day-summary",
            expected_count_text,
            "Details anzeigen",
        ):
            if expected not in collapsed_week_page:
                raise AssertionError(
                    f"Eingeklappte Wochenansicht enthält {expected!r} nicht"
                )

        if re.search(
            r'<details class="card appointment-day appointment-week-day-collapsible"\s+open',
            collapsed_week_page,
        ):
            raise AssertionError("Mehrfach belegter Wochentag ist bereits geöffnet")

        month_response = raw_request(request_text(
            "GET",
            f"/admin/appointments?view=month&date={row[0]}",
            auth_headers,
        ).encode())
        _, month_body = assert_status(month_response, "200 OK")
        month_page = month_body.decode("utf-8")
        parsed_month_date = datetime.strptime(row[0], "%Y-%m-%d")
        month_names = (
            "Januar", "Februar", "März", "April", "Mai", "Juni",
            "Juli", "August", "September", "Oktober", "November", "Dezember",
        )
        expected_month_label = (
            f"{month_names[parsed_month_date.month - 1]} {parsed_month_date.year}"
        )
        expected_day_count = calendar.monthrange(
            parsed_month_date.year,
            parsed_month_date.month,
        )[1]

        for expected in (
            "Monat",
            expected_month_label,
            "appointment-month",
            "Details anzeigen",
            "Termin vorhanden",
            "Bestätigter Termin",
            "Sperrzeit oder Urlaub",
        ):
            if expected not in month_page:
                raise AssertionError(f"Monatsansicht enthält {expected!r} nicht")

        rendered_days = len(re.findall(
            r'<(?:article|details) class="appointment-month-day',
            month_page,
        ))
        if rendered_days != expected_day_count:
            raise AssertionError(
                f"Monatsansicht zeigt {rendered_days} statt {expected_day_count} Kalendertage"
            )
        if "30 Tage" in month_page:
            raise AssertionError("Alte 30-Tage-Bezeichnung ist noch sichtbar")



    def rate_limiting() -> None:
        postal_headers = proxy_headers("198.51.100.199")
        for _ in range(30):
            response = raw_request(request_text(
                "GET",
                "/api/postal-code?postal_code=26121",
                postal_headers,
            ).encode())
            assert_status(response, "200 OK")

        postal_limited = raw_request(request_text(
            "GET",
            "/api/postal-code?postal_code=26121",
            postal_headers,
        ).encode())
        postal_limit_headers, _ = assert_status(
            postal_limited,
            "429 Too Many Requests",
        )
        postal_retry = int(postal_limit_headers.get("retry-after", "0"))
        if postal_retry < 1 or postal_retry > 60:
            raise AssertionError(
                f"Ungültiger PLZ-Retry-After-Wert: {postal_retry}"
            )

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
            assert_status(response, "303 See Other")

        blocked_admin = raw_request(request_text(
            "GET",
            "/admin/bookings",
            invalid_admin_headers,
        ).encode())
        assert_status(blocked_admin, "303 See Other")

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
            assert_status(response, "303 See Other")

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
            assert_status(response, "303 See Other")

        reset_blocked = raw_request(request_text(
            "GET",
            "/admin/bookings",
            reset_bad_headers,
        ).encode())
        assert_status(reset_blocked, "303 See Other")

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
    check("PLZ-API ergänzt den Wohnort", postal_code_lookup)
    check("Öffentlicher Kalender reserviert einen Pending-Termin", public_calendar_and_pending_booking)
    check("Einmaliges Admin-Setup und Basic Auth", first_run_admin_setup)
    check("Sitzungsbasierte Admin-Anmeldung, CSRF und Logout", admin_session_workflow)
    check("Admin sieht das zentrale Dashboard", admin_dashboard_overview)
    check("Admin lädt Galeriebilder hoch und löscht sie", admin_gallery_workflow)
    check("Admin sortiert Galeriebilder dauerhaft", admin_gallery_reorder_workflow)
    check("Admin kann einen Buchungsstatus sicher ändern", admin_status_workflow)
    check("Admin kann Buchungen filtern und durchsuchen", admin_filter_workflow)
    check("Admin nimmt Terminanfragen an oder lehnt sie ab", admin_booking_decisions)
    check("Admin speichert Kalendereinstellungen gemeinsam", admin_calendar_workflow)
    check("Honeypot und Kontaktlimit schützen vor Quatschbuchungen", booking_spam_protection)
    check("Admin verbindet ein E-Mail-Konto und reiht eine Testmail ein", admin_email_connection)
    check("Freie Termine können automatisch bestätigt werden", automatic_confirmation)
    check("Admin individualisiert Bestätigungen, Absagen und bereinigt die Queue", admin_message_templates)
    check("Kunden sehen und stornieren ihre Buchung über einen sicheren Link", customer_portal_cancellation)
    check("Phase 14 verwaltet Verschiebung, Historie, Stammdaten und No-Show", phase14_booking_management)
    check("Admin sieht Termine in Tages-, Wochen- und Monatsansicht", admin_appointments_workflow)
    check("Rate-Limits schützen PLZ-Abfrage, Buchung und Adminzugang", rate_limiting)

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
        ("PLZ-JavaScript", "/postal-code.js"),
        ("Kalender-JavaScript", "/calendar.js"),
        ("Admin-Kalender-JavaScript", "/admin-calendar.js"),
        ("Admin-E-Mail-JavaScript", "/admin-notifications.js"),
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
    add_status(cannon, "Adminbereich ohne Zugangsdaten", request_text("GET", "/admin/bookings"), "303 See Other")
    add_status(cannon, "Admin-Kalender ohne Zugangsdaten", request_text("GET", "/admin/calendar"), "303 See Other")
    add_status(cannon, "Admin-Terminkalender ohne Zugangsdaten", request_text("GET", "/admin/appointments"), "303 See Other")
    add_status(cannon, "Admin-Galerie ohne Zugangsdaten", request_text(
        "GET",
        "/admin/gallery",
        proxy_headers("198.51.100.90"),
    ), "303 See Other")
    add_status(cannon, "Adminbereich mit ungültigem Basic Token", request_text(
        "GET", "/admin/bookings", {"Authorization": "Basic !!!"}
    ), "303 See Other")
    add_status(cannon, "Statusänderung ohne Zugangsdaten", request_text(
        "POST",
        "/admin/bookings/status",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "booking_id=1&status=kontaktiert&csrf_token=invalid",
    ), "303 See Other")
    add_status(cannon, "Kalenderänderung ohne Zugangsdaten", request_text(
        "POST",
        "/admin/calendar/settings",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "min_notice_hours=0&booking_horizon_days=90&slot_interval_minutes=15&pending_hold_hours=24&reminder_lead_hours=24&csrf_token=invalid",
    ), "303 See Other")
    add_status(cannon, "Terminannahme ohne Zugangsdaten", request_text(
        "POST",
        "/admin/bookings/accept",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "booking_id=1&csrf_token=invalid",
    ), "303 See Other")
    add_status(cannon, "Leistung anlegen ohne Zugangsdaten", request_text(
        "POST",
        "/admin/calendar/service/add",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "code=test&name=Test&duration_minutes=30&buffer_minutes=0&csrf_token=invalid",
    ), "303 See Other")
    add_status(cannon, "Setup-Seite beim ersten Start", request_text("GET", "/setup/admin"), "200 OK")

    missing_slot_booking = urlencode({
        "name": "Laz0r Test",
        **email_contact("laz0r@example.invalid"),
        **address_fields(),
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
        **address_fields(),
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
        **address_fields(),
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
        **address_fields(),
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

    invalid_postal_code = urlencode({
        "name": "Ungültige Postleitzahl",
        **email_contact("postal@example.invalid"),
        **address_fields(postal_code="2612A"),
        "dog_size": "small",
        "service": "wash_dry",
        "appointment_date": "2026-08-20",
        "appointment_start": "09:00",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Nicht numerische Postleitzahl wird abgelehnt", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.146",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        invalid_postal_code,
    ), "400 Bad Request")

    missing_house_number = urlencode({
        "name": "Fehlende Hausnummer",
        **email_contact("street@example.invalid"),
        **address_fields(street_address="Teststraße"),
        "dog_size": "small",
        "service": "wash_dry",
        "appointment_date": "2026-08-20",
        "appointment_start": "09:00",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Straße ohne Hausnummer wird abgelehnt", request_text(
        "POST",
        "/booking",
        proxy_headers(
            "198.51.100.147",
            {"Content-Type": "application/x-www-form-urlencoded"},
        ),
        missing_house_number,
    ), "400 Bad Request")

    invalid_whatsapp_landline = urlencode({
        "name": "Ungültiger WhatsApp-Kontakt",
        **phone_contact("02571 123456", "landline", "whatsapp"),
        **address_fields(),
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
        **address_fields(),
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
