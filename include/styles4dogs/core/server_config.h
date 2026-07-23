#ifndef STYLES4DOGS_CORE_SERVER_CONFIG_H
#define STYLES4DOGS_CORE_SERVER_CONFIG_H

/**
 * @file server_config.h
 * @brief Lädt und validiert die Laufzeitkonfiguration des Servers.
 */

#include <stdint.h>

/**
 * @brief Lädt und validiert die Laufzeitkonfiguration aus Umgebungsvariablen.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int server_config_initialize(void);

/**
 * @brief Liefert die letzte Konfigurationsfehlermeldung.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_last_error(void);

/**
 * @brief Liefert die konfigurierte Bind-Adresse des Backendservers.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_bind_address(void);
/**
 * @brief Liefert den konfigurierten TCP-Port des Backendservers.
 * @return Ergebniswert vom Typ `uint16_t`.
 */
uint16_t server_config_port(void);
/**
 * @brief Liefert das Verzeichnis der statischen Website-Dateien.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_document_root(void);
/**
 * @brief Liefert das Verzeichnis für geheime Laufzeitdaten.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_secrets_dir(void);
/**
 * @brief Liefert den Pfad zur Admin-Authentifizierungsdatei.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_auth_file(void);
/**
 * @brief Liefert das Laufzeitdatenverzeichnis.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_data_dir(void);
/**
 * @brief Liefert den Pfad zur SQLite-Datenbank.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_database_file(void);
/**
 * @brief Liefert den Pfad zur einmalig importierten TSV-Datei.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_legacy_booking_file(void);

/**
 * @brief Liefert das Secret für vertrauenswürdige Proxy-Header.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_trusted_proxy_token(void);

/**
 * @brief Liefert den konfigurierten Salonnamen.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_salon_name(void);
/**
 * @brief Liefert die konfigurierte Salonadresse.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_salon_address(void);
/**
 * @brief Liefert die konfigurierte Salontelefonnummer.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_salon_phone(void);
/**
 * @brief Liefert die öffentliche Basis-URL für Kundenlinks.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_public_base_url(void);
/**
 * @brief Liefert den Standard-Ländercode für lokale Telefonnummern.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_default_phone_country_code(void);

/**
 * @brief Liefert den fest konfigurierten PLZ-Dienstendpunkt.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *server_config_postal_lookup_base_url(void);

#endif
