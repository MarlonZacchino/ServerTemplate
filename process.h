//
// Created by Marlon on 11.07.26.
//

#ifndef SERVER_PROCESS_H
#define SERVER_PROCESS_H

#include "http_lib.h"

/*
 * Verarbeitet einen HTTP-Request und gibt eine fertige HTTP-Response zurück.
 *
 * Der request gehört weiterhin dem Aufrufer.
 * Die zurückgegebene response muss später mit free_str() freigegeben werden.
 */
string *process(string *request);

#endif