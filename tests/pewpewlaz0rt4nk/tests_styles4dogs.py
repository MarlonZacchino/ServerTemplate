#!/usr/bin/env python3
"""HTTP regression tests for the Styles 4 Dogs C server."""

from __future__ import annotations

import base64
import os
import re
import socket
import sqlite3
import sys
from pathlib import Path
from urllib.parse import urlencode

from pewpewlaz0rt4nk import Beam, Laz0rCannon

HOST = sys.argv[1] if len(sys.argv) >= 2 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) >= 3 else 31337
TIMEOUT = 5
TEST_ADMIN_USERNAME = "test-admin"
TEST_ADMIN_PASSWORD = "Styles4Dogs-Test-2026!"


def request_text(method: str, path: str, headers: dict[str, str] | None = None, body: str = "") -> str:
    all_headers = {"Host": f"{HOST}:{PORT}", **(headers or {})}
    if body and "Content-Length" not in all_headers:
        all_headers["Content-Length"] = str(len(body.encode("utf-8")))

    header_lines = "".join(f"{name}: {value}\r\n" for name, value in all_headers.items())
    return f"{method} {path} HTTP/1.1\r\n{header_lines}\r\n{body}"


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
            before_count = connection.execute(
                "SELECT COUNT(*) FROM bookings"
            ).fetchone()[0]

            legacy_row = connection.execute(
                "SELECT status, customer_name, contact, dog_name, message, legacy "
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
            ):
                raise AssertionError("Das frühere fünfspaltige TSV-Format wurde nicht korrekt importiert")

            imported_v2 = connection.execute(
                "SELECT status, dog_size, service, preferred_date, legacy "
                "FROM bookings WHERE customer_name = ?",
                ("TSV V2 Test",),
            ).fetchone()

            if imported_v2 != ("neu", "small", "wash_dry", "2026-08-12", 0):
                raise AssertionError("Das TSV-v2-Format wurde nicht korrekt importiert")

            migration_marker = connection.execute(
                "SELECT value FROM app_metadata WHERE key = ?",
                ("legacy_tsv_import_v1",),
            ).fetchone()

            if migration_marker is None or not migration_marker[0].startswith("completed:2"):
                raise AssertionError("Der einmalige TSV-Import wurde nicht als abgeschlossen markiert")

        body = urlencode({
            "name": "Pew Pew Test",
            "contact": "test@example.invalid",
            "dog_name": "Bello",
            "dog_size": "medium",
            "service": "full_groom",
            "preferred_date": "2026-08-20",
            "message": "Zeile 1\nZeile 2",
            "privacy_consent": "accepted",
        })
        response = raw_request(request_text(
            "POST",
            "/booking",
            {"Content-Type": "application/x-www-form-urlencoded"},
            body,
        ).encode())
        assert_status(response, "201 Created")

        with sqlite3.connect(database_file) as connection:
            after_count = connection.execute(
                "SELECT COUNT(*) FROM bookings"
            ).fetchone()[0]

            if after_count != before_count + 1:
                raise AssertionError(
                    f"Erwartet genau eine neue SQLite-Zeile, Anzahl vorher={before_count}, nachher={after_count}"
                )

            row = connection.execute(
                "SELECT status, customer_name, contact, dog_name, dog_size, service, "
                "preferred_date, message, legacy "
                "FROM bookings ORDER BY id DESC LIMIT 1"
            ).fetchone()

        expected = (
            "neu",
            "Pew Pew Test",
            "test@example.invalid",
            "Bello",
            "medium",
            "full_groom",
            "2026-08-20",
            "Zeile 1\nZeile 2",
            0,
        )

        if row != expected:
            raise AssertionError(f"SQLite-Buchung wurde nicht korrekt gespeichert: {row!r}")

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

    check("GET/HEAD Content-Length und leerer HEAD-Body", content_length_and_head)
    check("Buchung wird isoliert und escaped gespeichert", booking_persistence)
    check("Einmaliges Admin-Setup und Basic Auth", first_run_admin_setup)
    check("Admin kann einen Buchungsstatus sicher ändern", admin_status_workflow)
    check("Admin kann Buchungen filtern und durchsuchen", admin_filter_workflow)

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
    ]

    for name, path in public_routes:
        add_status(cannon, f"GET {name}", request_text("GET", path), "200 OK")

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
    add_status(cannon, "Adminbereich mit ungültigem Basic Token", request_text(
        "GET", "/admin/bookings", {"Authorization": "Basic !!!"}
    ), "401 Unauthorized")
    add_status(cannon, "Statusänderung ohne Zugangsdaten", request_text(
        "POST",
        "/admin/bookings/status",
        {"Content-Type": "application/x-www-form-urlencoded"},
        "booking_id=1&status=kontaktiert&csrf_token=invalid",
    ), "401 Unauthorized")
    add_status(cannon, "Setup-Seite beim ersten Start", request_text("GET", "/setup/admin"), "200 OK")

    valid_booking = urlencode({
        "name": "Laz0r Test",
        "contact": "laz0r@example.invalid",
        "dog_name": "Flocke",
        "dog_size": "small",
        "service": "wash_dry",
        "preferred_date": "2026-08-21",
        "message": "Regressionstest",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Gültige Buchungsanfrage", request_text(
        "POST",
        "/booking",
        {"Content-Type": "application/x-www-form-urlencoded"},
        valid_booking,
    ), "201 Created")

    invalid_booking = urlencode({"name": "Ohne Kontakt"})
    add_status(cannon, "Buchung ohne Pflichtfeld", request_text(
        "POST",
        "/booking",
        {"Content-Type": "application/x-www-form-urlencoded"},
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
        {"Content-Type": "application/x-www-form-urlencoded"},
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
        {"Content-Type": "application/x-www-form-urlencoded"},
        invalid_service,
    ), "400 Bad Request")

    invalid_date = urlencode({
        "name": "Ungültiges Datum",
        "contact": "date@example.invalid",
        "dog_size": "medium",
        "service": "full_groom",
        "preferred_date": "2026-02-31",
        "privacy_consent": "accepted",
    })
    add_status(cannon, "Buchung mit ungültigem Wunschdatum", request_text(
        "POST",
        "/booking",
        {"Content-Type": "application/x-www-form-urlencoded"},
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
