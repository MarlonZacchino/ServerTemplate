#!/usr/bin/env python3
"""HTTP regression tests for the Styles 4 Dogs C server."""

from __future__ import annotations

import base64
import os
import re
import socket
import sys
from pathlib import Path
from urllib.parse import urlencode

from pewpewlaz0rt4nk import Beam, Laz0rCannon

HOST = sys.argv[1] if len(sys.argv) >= 2 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) >= 3 else 31337
TIMEOUT = 5


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
        booking_file_value = os.environ.get("STYLES4DOGS_TEST_BOOKING_FILE")
        if not booking_file_value:
            raise AssertionError("STYLES4DOGS_TEST_BOOKING_FILE fehlt")

        booking_file = Path(booking_file_value)
        before = booking_file.read_text(encoding="utf-8") if booking_file.exists() else ""
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

        after = booking_file.read_text(encoding="utf-8")
        added = after[len(before):]
        lines = [line for line in added.splitlines() if line]
        if len(lines) != 1:
            raise AssertionError(f"Erwartet genau eine neue TSV-Zeile, erhalten {len(lines)}")

        fields = lines[0].split("\t")
        if len(fields) != 10:
            raise AssertionError(f"V2-Buchung enthält {len(fields)} statt 10 Felder")
        if fields[0] != "v2" or fields[2] != "neu":
            raise AssertionError("Version oder initialer Buchungsstatus ist falsch")
        if fields[3:9] != [
            "Pew Pew Test",
            "test@example.invalid",
            "Bello",
            "medium",
            "full_groom",
            "2026-08-20",
        ]:
            raise AssertionError("Die erweiterten Buchungsfelder wurden nicht korrekt gespeichert")
        if fields[9] != "Zeile 1\\nZeile 2":
            raise AssertionError("Zeilenumbruch wurde in der TSV-Datei nicht escaped")

        with booking_file.open("a", encoding="utf-8") as file:
            file.write(
                "2026-07-01 12:00:00\tLegacy Test\tlegacy@example.invalid"
                "\tWaldi\tFrühere Anfrage\n"
            )

    def first_run_admin_setup() -> None:
        setup_response = raw_request(request_text("GET", "/setup/admin").encode())
        setup_headers, setup_body = assert_status(setup_response, "200 OK")

        if setup_headers.get("cache-control") != "no-store":
            raise AssertionError("Setup-Seite setzt Cache-Control: no-store nicht")

        match = re.search(rb'name="csrf_token" value="([0-9a-f]+)"', setup_body)
        if match is None:
            raise AssertionError("CSRF-Token wurde nicht in der Setup-Seite gefunden")

        username = "test-admin"
        password = "Styles4Dogs-Test-2026!"
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

    check("GET/HEAD Content-Length und leerer HEAD-Body", content_length_and_head)
    check("Buchung wird isoliert und escaped gespeichert", booking_persistence)
    check("Einmaliges Admin-Setup und Basic Auth", first_run_admin_setup)

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
