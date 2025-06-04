/*
 * File: client.c
 * Descrizione: Client TCP per comunicazione con server stampante fiscale
 *              Implementa la logica di invio comandi e ricezione risposte
 */

// Disabilita warning per funzioni deprecate di Winsock e CRT
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

// Definizione dei colori per la console
#define COLOR_DEFAULT 7   // Bianco
#define COLOR_INFO 10     // Verde per info
#define COLOR_ERROR 12    // Rosso per errori
#define COLOR_WARNING 14  // Giallo per warning
#define COLOR_SUCCESS 10  // Verde per successo
#define COLOR_TITLE 11    // Azzurro per titoli
#define COLOR_SECTION 13  // Magenta per sezioni


// Costanti per la separazione dei comandi
#define SEPARATOR "------------------------------------------------------------"
#define SEPARATOR_COLOR 8 // Grigio scuro

// Inclusione delle librerie necessarie
#define __STDC_WANT_LIB_EXT1__ 1  // Per strtok_s
#include <stdio.h>      // I/O standard
#include <string.h>     // Funzioni stringhe
#include <ctype.h>
#include <winsock2.h>   // Socket Windows
#include <windows.h>    // Funzioni Windows
#include <stdlib.h>     // Funzioni standard
#include "error_table.h"     // Funzioni standard

// Dichiarazione esplicita di strtok_s per compatibilità
char* strtok_s(char* str, const char* delim, char** context);

// Linka automaticamente la libreria ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

// Prototipi delle funzioni
void mostra_comandi();
void print_separator();
void print_colored(const char* msg, int color);
void mostra_stato(const char* comando, const char* risposta, int successo);
void stampa_risposta_server(char* campo_dati);

/*
 * Funzioni di utilità per l'interfaccia utente
 */

// Imposta il colore del testo nella console
void set_color(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

// Mostra la lista dei comandi disponibili
void mostra_comandi() {
    set_color(COLOR_TITLE);
    printf("Comandi disponibili:\n");
    
    set_color(COLOR_DEFAULT);
    printf("  [REGISTRAZIONE]\n");
    set_color(COLOR_SECTION);
    printf("    Rxx/$yyyy   - Registra importo\n");
    printf("    a           - Annulla ultimo importo\n");
    set_color(COLOR_DEFAULT);
    printf("  [TOTALI]\n");
    set_color(COLOR_SECTION);
    printf("    S           - Subtotale\n");
    printf("    T1          - Totale in contanti\n");
    set_color(COLOR_DEFAULT);
    printf("  [CONTROLLO]\n");
    set_color(COLOR_SECTION);
    printf("    =K          - Reset stampante\n");
    printf("    esc         - Esci\n");
    set_color(COLOR_DEFAULT);
}

// Stampa una linea di separazione per migliorare la leggibilità
void print_separator() {
    set_color(SEPARATOR_COLOR);
    printf("%s\n", SEPARATOR);
}

// Funzione per analizzare la risposta del server
int analizza_risposta(const char* risposta, char* tipo_messaggio, char* famiglia_errore, char* codice_errore, char* messaggio, int max_messaggio) {
    // Formato atteso: TIPO|FAMIGLIA|CODICE|MESSAGGIO
    char* token;
    char* context = NULL;
    char temp[1024];
    
    // Crea una copia della stringa per evitare di modificare l'originale
    strncpy(temp, risposta, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    // Estrai tipo messaggio
    token = strtok_s(temp, "|", &context);
    if (!token) return 0;
    *tipo_messaggio = token[0];
    
    // Estrai famiglia errore
    token = strtok_s(NULL, "|", &context);
    if (!token) return 0;
    *famiglia_errore = token[0];
    
    // Estrai codice errore
    token = strtok_s(NULL, "|", &context);
    if (!token) return 0;
    strncpy(codice_errore, token, 5);
    codice_errore[4] = '\0';
    
    // Estrai messaggio
    token = strtok_s(NULL, "\n", &context);
    if (!token) return 0;
    strncpy(messaggio, token, max_messaggio - 1);
    messaggio[max_messaggio - 1] = '\0';
    
    return 1;
}

// Mostra lo stato di un'operazione
void mostra_stato(const char* comando, const char* risposta, int successo) {
    char tipo_messaggio = 'O';
    char famiglia_errore = 'N';
    char codice_errore[16] = "0000";
    char messaggio[1024] = "";
    
    // Analizza la risposta se è nel formato atteso
    int formato_valido = analizza_risposta(risposta, &tipo_messaggio, &famiglia_errore, codice_errore, messaggio, sizeof(messaggio));
    
    if (!formato_valido) {
        // Formato non riconosciuto, mostra la risposta così com'è
        printf("%s: %s - %s\n", comando, risposta, successo ? "SUCCESSO" : "ERRORE");
        return;
    }
    
    // Stampa il messaggio formattato
    if (tipo_messaggio == 'O') {
        set_color(COLOR_SUCCESS);
        printf("%s: %s - SUCCESSO (Codice: %s)\n", comando, messaggio, codice_errore);
    } else {
        set_color(COLOR_ERROR);
        const char* tipo_errore = "Generico";
        
        // Determina il tipo di errore in base alla famiglia
        switch (famiglia_errore) {
            case 'G': tipo_errore = "Generico"; break;
            case 'S': tipo_errore = "Bloccante"; break;
            case 'P': tipo_errore = "Fine carta"; break;
            default:  tipo_errore = "Sconosciuto";
        }
        
        printf("%s: ERRORE %s (Codice: %s) - %s\n", 
               comando, tipo_errore, codice_errore, messaggio);
    }
    set_color(COLOR_DEFAULT);
}

// Mostra un messaggio di conferma per un comando
void mostra_conferma_comando(const char* comando) {
    printf("\nComando inviato correttamente: %s\n", comando);
    printf("In attesa di risposta dal server...\n");
    print_separator();
}

int main() {
    WSADATA wsa; // Struttura che contiene informazioni sulla versione di Winsock
    SOCKET sock; // Variabile per il socket
    struct sockaddr_in server; // Struttura per l'indirizzo del server
    char message[1024], server_reply[1024] = {0}; // Buffer per messaggi e risposte
    int recv_size = 0;  // Dichiara qui per renderla accessibile in tutto il main
 
    printf("+----------------------------------------------------------+\n");
    printf("|                CLIENT TCP - CONSOLE v1.3.1               |\n");
    printf("+----------------------------------------------------------+\n");
    printf("\nInformazioni di sistema:\n");
    printf("- Versione client: 1.3.1\n");
    // --- Configurazione dinamica IP e porta ---
    char ip_server[64] = "10.0.70.14";
    char porta_str[16] = "9999";
    int porta = 9999;
    printf("- Server di default: %s:%d\n", ip_server, porta);
    printf("- Protocollo: TCP\n");
    print_separator();
    // Richiedi IP
    printf("Inserisci IP server [default: %s]: ", ip_server);
    fgets(ip_server, sizeof(ip_server), stdin);
    ip_server[strcspn(ip_server, "\n")] = 0;
    if (strlen(ip_server) == 0) strcpy(ip_server, "10.0.70.14");
    // Richiedi porta
    printf("Inserisci porta server [default: %d]: ", porta);
    fgets(porta_str, sizeof(porta_str), stdin);
    porta_str[strcspn(porta_str, "\n")] = 0;
    if (strlen(porta_str) > 0) porta = atoi(porta_str);
    // Stampa riepilogo connessione
    printf("[INFO] Connessione a %s:%d\n", ip_server, porta);
    printf("[INFO] Modalità debug attiva: i messaggi di errore saranno dettagliati\n");
    print_separator();
    
    printf("Inizializzo Winsock...\n");
    // Inizializza la libreria Winsock (versione 2.2)
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        set_color(12);
        printf("[X] Errore Winsock: %d\n", WSAGetLastError());
        set_color(7);
        WSACleanup();
        return 1;
    }
 
    // Crea un socket TCP (SOCK_STREAM) IPv4 (AF_INET)
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        set_color(12);
        printf("[X] Errore creazione socket: %d\n", WSAGetLastError());
        set_color(7);
        WSACleanup();
        return 1;
    }
 
    // Imposta l'indirizzo IP e la porta del server a cui connettersi
    server.sin_addr.s_addr = inet_addr(ip_server); // Converte stringa IP in formato binario
    server.sin_family = AF_INET; // Famiglia di indirizzi IPv4
    server.sin_port = htons(porta); // Converte la porta in formato network byte order
    
    // Imposta un timeout di connessione di 5 secondi
    DWORD timeout = 5000; // 5 secondi in millisecondi
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Stampa informazioni di debug sulla connessione
    printf("\n[DEBUG] Tentativo di connessione a %s:%d...\n", ip_server, porta);
    
    // Prova a connetterti al server
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("[DEBUG] Errore connessione: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    
    printf("[DEBUG] Connessione stabilita con successo\n");
    
    // Imposta il socket in modalità non bloccante
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    set_color(10); // Verde
    printf("[OK] Connesso al server.\n");
    set_color(7);
    
    // Mostra la lista dei comandi disponibili
    mostra_comandi();
    print_separator();
    // Mostra suggerimenti utili
    printf("\nSuggerimenti:\n");
    printf("- Usa =C1 per attivare la modalità REG\n");
    printf("- Usa =K per resettare la stampante\n");
    printf("- Usa =k per annullare il documento\n");
    printf("- Usa ? per vedere lo stato corrente\n");
    print_separator();
    print_separator();

    // Ciclo principale di invio/ricezione messaggi
    while (1) {
        print_separator();
        set_color(14); // Giallo
        printf("[;P] Inserisci il tuo messaggio\n");
        set_color(13); // Magenta
        printf(" (esc = esci, multi = invio multiplo, help = mostra comandi)\n");
        set_color(10); // Verde chiaro
        printf("> ");
        set_color(7);
        fflush(stdout);  // Assicura che il prompt venga mostrato
        
        // Pulisce il buffer di input prima di leggere
        fseek(stdin, 0, SEEK_END);
        
        // Legge l'input
        if (fgets(message, sizeof(message), stdin) == NULL) {
            // Se c'è un errore o EOF, esci
            strcpy(message, "esc");
        } else {
            // Rimuove il carattere di newline finale
            message[strcspn(message, "\n")] = 0;
        }
 
        // Se l'utente digita "esc", esce dal ciclo
        if (strcmp(message, "esc") == 0) {
            break;
        }
        
        // Mostra aiuto
        if (strcmp(message, "help") == 0) {
            mostra_comandi();
            continue;
        }

        // Modalità multi-comando: raccogli tutti i comandi, poi inviali in sequenza (nuova connessione per ogni comando)
        if (strcmp(message, "multi") == 0) {
            #define MAX_MULTI 50
            char multi_cmds[MAX_MULTI][1024];
            int n_multi = 0;
            while (n_multi < MAX_MULTI) {
                printf("> ");
                fflush(stdout);
                if (fgets(multi_cmds[n_multi], sizeof(multi_cmds[n_multi]), stdin) == NULL) break;
                multi_cmds[n_multi][strcspn(multi_cmds[n_multi], "\n")] = 0;
                if (strlen(multi_cmds[n_multi]) == 0) break; // riga vuota = fine batch
                n_multi++;
            }
            for (int i = 0; i < n_multi; ++i) {
                // --- Riapri connessione per ogni comando ---
                SOCKET sock_multi;
                struct sockaddr_in server_multi;
                sock_multi = socket(AF_INET, SOCK_STREAM, 0);
                if (sock_multi == INVALID_SOCKET) {
                    set_color(COLOR_ERROR);
                    printf("[X] Errore creazione socket: %d\n", WSAGetLastError());
                    set_color(COLOR_DEFAULT);
                    continue;
                }
                server_multi.sin_addr.s_addr = inet_addr(ip_server);
                server_multi.sin_family = AF_INET;
                server_multi.sin_port = htons(porta);
                if (connect(sock_multi, (struct sockaddr*)&server_multi, sizeof(server_multi)) < 0) {
                    set_color(COLOR_ERROR);
                    printf("[X] Errore connessione: %d\n", WSAGetLastError());
                    set_color(COLOR_DEFAULT);
                    closesocket(sock_multi);
                    continue;
                }
                char to_send[1024];
                int sent;
                snprintf(to_send, sizeof(to_send), "%s\r\n", multi_cmds[i]);
#ifdef DEBUG_PROTOCOL
                printf("[DEBUG] Invio comando (multi #%d): %s\n", i+1, multi_cmds[i]);
#endif
                sent = send(sock_multi, to_send, (int)strlen(to_send), 0);
#ifdef DEBUG_PROTOCOL
                printf("[DEBUG] Bytes inviati: %d\n", sent);
#endif
                if (sent < 0) {
                    set_color(COLOR_ERROR);
                    printf("[X] Errore invio messaggio: %d\n", WSAGetLastError());
                    set_color(COLOR_DEFAULT);
                    closesocket(sock_multi);
                    continue;
                }
                memset(server_reply, 0, sizeof(server_reply));
                int recv_size = recv(sock_multi, server_reply, sizeof(server_reply) - 1, 0);
                if (recv_size <= 0) {
                    set_color(COLOR_ERROR);
                    printf("[X] Errore o connessione chiusa dal server.\n");
                    set_color(COLOR_DEFAULT);
                    closesocket(sock_multi);
                    continue;
                }
                printf("Risposta dal server (multi #%d):\n", i+1);
                set_color(10);
                // Estrazione campo dati da pacchetto protocollo
                char campo_dati[1024] = "";
                int len = (int)strlen(server_reply);
                if (len > 8 && (unsigned char)server_reply[0] == 0x02 && (unsigned char)server_reply[len-1] == 0x03) {
                    // [STX][adds][len][N][dati][pack_id][CHK][ETX]
                    int dati_len = ((server_reply[5]-'0')*100 + (server_reply[6]-'0')*10 + (server_reply[7]-'0'));
                    if (dati_len > 0 && dati_len < (int)sizeof(campo_dati) && 8+dati_len <= len-3) {
                        memcpy(campo_dati, &server_reply[9], dati_len);
                        campo_dati[dati_len] = 0;
                    } else {
                        strncpy(campo_dati, server_reply, sizeof(campo_dati)-1);
                        campo_dati[sizeof(campo_dati)-1] = 0;
                    }
                } else {
                    strncpy(campo_dati, server_reply, sizeof(campo_dati)-1);
                    campo_dati[sizeof(campo_dati)-1] = 0;
                }
                stampa_risposta_server(campo_dati);
                closesocket(sock_multi);
            }
            continue;
        }

        char to_send[1024];
        int sent;
        snprintf(to_send, sizeof(to_send), "%s\r\n", message);
#ifdef DEBUG_PROTOCOL
        printf("[DEBUG] Invio comando: %s\n", message);
#endif
        sent = send(sock, to_send, (int)strlen(to_send), 0);
#ifdef DEBUG_PROTOCOL
        printf("[DEBUG] Bytes inviati: %d\n", sent);
#endif
        if (sent < 0) {
            set_color(COLOR_ERROR);
            printf("[X] Errore invio messaggio: %d\n", WSAGetLastError());
            set_color(COLOR_DEFAULT);
            continue;
        }

        // Ricevi la risposta dal server
        memset(server_reply, 0, sizeof(server_reply));
        int recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
        if (recv_size <= 0) {
            set_color(COLOR_ERROR);
            printf("[X] Errore o connessione chiusa dal server.\n");
            set_color(COLOR_DEFAULT);
            continue;
        }

        // Stampa la risposta in modo più leggibile
        printf("Risposta dal server:\n");
        set_color(10);
        // --- Decodifica campo dati come nel multi ---
        char campo_dati[1024] = "";
        int len = (int)strlen(server_reply);
        if (len > 8 && (unsigned char)server_reply[0] == 0x02 && (unsigned char)server_reply[len-1] == 0x03) {
            int dati_len = ((server_reply[5]-'0')*100 + (server_reply[6]-'0')*10 + (server_reply[7]-'0'));
            if (dati_len > 0 && dati_len < (int)sizeof(campo_dati) && 8+dati_len <= len-3) {
                memcpy(campo_dati, &server_reply[9], dati_len);
                campo_dati[dati_len] = 0;
            } else {
                strncpy(campo_dati, server_reply, sizeof(campo_dati)-1);
                campo_dati[sizeof(campo_dati)-1] = 0;
            }
        } else {
            strncpy(campo_dati, server_reply, sizeof(campo_dati)-1);
            campo_dati[sizeof(campo_dati)-1] = 0;
        }
        stampa_risposta_server(campo_dati);
    }
    // Codice di pulizia e chiusura di main
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
    WSACleanup();

    set_color(COLOR_SUCCESS);
    printf("Disconnesso dal server.\n");
    set_color(COLOR_DEFAULT);
    return 0;
}

void stampa_risposta_server(char* campo_dati) {
    // Prima cerca la sequenza di stato diretta della stampante (es. ESxxxx, ONxxxx)
    if (strlen(campo_dati) >= 6) { // Lunghezza minima per 'EXxxxx' o 'OXxxxx'
        for (size_t i = 0; i <= strlen(campo_dati) - 6; ++i) { // Modificato il limite del ciclo per evitare out-of-bounds
            char tipo = toupper((unsigned char)campo_dati[i]);
            char famiglia = toupper((unsigned char)campo_dati[i+1]);
            // Verifica che i caratteri 2,3,4,5 siano cifre
            if ((tipo == 'E' || tipo == 'O') &&
                (famiglia == 'N' || famiglia == 'G' || famiglia == 'S' || famiglia == 'P') &&
                isdigit((unsigned char)campo_dati[i+2]) &&
                isdigit((unsigned char)campo_dati[i+3]) &&
                isdigit((unsigned char)campo_dati[i+4]) &&
                isdigit((unsigned char)campo_dati[i+5])) {
                
                char codice_errore_diretto[5];
                strncpy(codice_errore_diretto, &campo_dati[i+2], 4);
                codice_errore_diretto[4] = '\0';
                
                const char* descr_diretto = descrizione_errore(codice_errore_diretto);
                
                if (tipo == 'E') set_color(COLOR_ERROR);
                else set_color(COLOR_SUCCESS);
                
                // Stampa il messaggio formattato usando i dati diretti
                printf("[DEBUG] Campo dati: %s\n", campo_dati); // Mostra sempre il campo dati per debug
                printf("[%s%s %s]\n", 
                       tipo == 'E' ? "ERRORE " : "OK ", 
                       famiglia == 'S' ? "BLOCCANTE" : 
                       famiglia == 'G' ? "GENERICO" : 
                       famiglia == 'P' ? "FINE CARTA" : 
                       famiglia == 'N' ? "NESSUNO" : "", 
                       codice_errore_diretto);
                if (descr_diretto) {
                    printf("Descrizione: %s\n", descr_diretto);
                }
                set_color(COLOR_DEFAULT);
                return; // Trovato e gestito, esci dalla funzione
            }
        }
    }

    // Se non è stata trovata una sequenza diretta, prova il parsing con analizza_risposta (fallback)
    char tipo_msg_fallback = 0, famiglia_errore_fallback = 0, codice_errore_fallback[8] = "", messaggio_fallback[512] = "";
    int riconosciuta_fallback = analizza_risposta(campo_dati, &tipo_msg_fallback, &famiglia_errore_fallback, codice_errore_fallback, messaggio_fallback, sizeof(messaggio_fallback));
    const char* descr_fallback = NULL;

    printf("[DEBUG] Campo dati (fallback): %s\n", campo_dati);

    if (riconosciuta_fallback) {
        descr_fallback = descrizione_errore(codice_errore_fallback);
        if (tipo_msg_fallback == 'E') set_color(COLOR_ERROR);
        else set_color(COLOR_SUCCESS);
    }
}