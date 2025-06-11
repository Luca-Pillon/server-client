#include "relay_control.h"
#include <stdio.h>
#include <string.h>
#include <windows.h> // Per CreateFile, etc.

// Handle globale per la porta seriale del relè
static HANDLE hRelay = INVALID_HANDLE_VALUE;

// Funzione interna per inviare comandi al relè
static void send_relay_command(const char* cmd) {
    if (hRelay == INVALID_HANDLE_VALUE) {
        // Non stampare errori qui per non intasare il log se il relè non è collegato.
        // La gestione dell'errore è a carico del chiamante tramite relay_is_ready()
        return;
    }

    DWORD bytesWritten;
    if (!WriteFile(hRelay, cmd, (DWORD)strlen(cmd), &bytesWritten, NULL)) {
        // Anche qui, gestiamo l'errore silenziosamente.
    }
}

void relay_init(const char* port) {
    char full_port_name[20];
    snprintf(full_port_name, sizeof(full_port_name), "\\\\.\\%s", port);

    hRelay = CreateFileA(full_port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       OPEN_EXISTING, 0, NULL);

    if (hRelay == INVALID_HANDLE_VALUE) {
        return; // Fallimento silenzioso, relay_is_ready() ritornerà 0
    }

    // Configura la porta seriale
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hRelay, &dcbSerialParams)) {
        CloseHandle(hRelay);
        hRelay = INVALID_HANDLE_VALUE;
        return;
    }

    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hRelay, &dcbSerialParams)) {
        CloseHandle(hRelay);
        hRelay = INVALID_HANDLE_VALUE;
        return;
    }

    // Imposta i timeout
    COMMTIMEOUTS timeouts = {0};
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hRelay, &timeouts)) {
        CloseHandle(hRelay);
        hRelay = INVALID_HANDLE_VALUE;
    }
}

void relay_on(void) {
    send_relay_command("AT+CH1=1\r\n");
}

void relay_off(void) {
    send_relay_command("AT+CH1=0\r\n");
}

int relay_is_ready(void) {
    return (hRelay != INVALID_HANDLE_VALUE);
}

void relay_cleanup(void) {
    if (hRelay != INVALID_HANDLE_VALUE) {
        relay_off(); // Invia il comando per spegnere il relè
        CloseHandle(hRelay); // Poi chiude la porta
        hRelay = INVALID_HANDLE_VALUE;
    }
}
