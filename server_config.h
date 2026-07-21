#ifndef STYLES4DOGS_SERVER_CONFIG_H
#define STYLES4DOGS_SERVER_CONFIG_H

#include <stdint.h>

/*
 * Lädt und validiert die Laufzeitkonfiguration aus Umgebungsvariablen.
 * Muss einmal vor Datenbank-, Authentifizierungs- und Request-Code aufgerufen
 * werden. Mehrfache erfolgreiche Aufrufe sind erlaubt.
 */
int server_config_initialize(void);

/* Letzte verständliche Initialisierungsfehlermeldung. */
const char *server_config_last_error(void);

const char *server_config_bind_address(void);
uint16_t server_config_port(void);
const char *server_config_document_root(void);
const char *server_config_secrets_dir(void);
const char *server_config_auth_file(void);
const char *server_config_data_dir(void);
const char *server_config_database_file(void);
const char *server_config_legacy_booking_file(void);

/*
 * Optional shared secret used to trust proxy-provided client IP headers.
 * An empty string disables forwarded-client-IP trust.
 */
const char *server_config_trusted_proxy_token(void);

/* Öffentliche Salonangaben für Nachrichten und Kontakt-Schnellaktionen. */
const char *server_config_salon_name(void);
const char *server_config_salon_address(void);
const char *server_config_salon_phone(void);
const char *server_config_public_base_url(void);
const char *server_config_default_phone_country_code(void);

#endif
