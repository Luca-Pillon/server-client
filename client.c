/*
 * File: client.c
 * Descrizione: Client TCP per comunicazione con server stampante fiscale
 *              Implementa la logica di invio comandi e ricezione risposte
 */

// Disabilita warning per funzioni deprecate di Winsock (ad es. inet_addr) e CRT (ad es. strcpy)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

// Definizione dei colori per la console (usati per migliorare la leggibilità dell'output)
#define COLOR_DEFAULT 7   // Bianco (testo normale)
#define COLOR_INFO 10     // Verde per informazioni generali
#define COLOR_ERROR 12    // Rosso per messaggi di errore
#define COLOR_WARNING 14  // Giallo per avvisi
#define COLOR_SUCCESS 10  // Verde per messaggi di successo
#define COLOR_TITLE 11    // Azzurro per titoli
#define COLOR_SECTION 13  // Magenta per sezioni
#define COLOR_INPUT 14    // Giallo per input utente

// Costanti per la separazione visiva dei comandi e delle sezioni nell'interfaccia
#define SEPARATOR "------------------------------------------------------------"
#define SEPARATOR_COLOR 8 // Grigio scuro (per linee di separazione)

// Inclusione delle librerie necessarie
#define __STDC_WANT_LIB_EXT1__ 1  // Richiesto per usare strtok_s (versione sicura di strtok)
#include <stdio.h>      // Input/Output standard (printf, scanf, ecc.)
#include <string.h>     // Funzioni per la manipolazione di stringhe (strcpy, strcmp, ecc.)
#include <ctype.h>      // Funzioni per la gestione di caratteri (isdigit, ecc.)
#include <winsock2.h>   // Libreria per socket su Windows (Winsock2)
#include <windows.h>    // Funzioni specifiche di Windows (colori console, ecc.)
#include <stdlib.h>     // Funzioni di utilità generale (malloc, free, system, ecc.)
#include "error_table.h"     // Definizione e gestione centralizzata dei codici di errore

// Dichiarazione esplicita di strtok_s per compatibilità con alcuni compilatori (es. MinGW)
char* strtok_s(char* str, const char* delim, char** context);

// Linka automaticamente la libreria ws2_32.lib necessaria per le funzioni di rete (Winsock)
#pragma comment(lib, "ws2_32.lib")

// Prototipi delle funzioni principali utilizzate nel client
void mostra_comandi(); // Mostra la lista dei comandi disponibili all'utente
void print_separator(); // Stampa una linea di separazione colorata
void print_colored(const char* msg, int color); // Stampa un messaggio con un colore specifico
void mostra_stato(const char* comando, const char* risposta, int successo); // Mostra l'esito di un'operazione
void stampa_risposta_server(char* campo_dati); // Stampa la risposta ricevuta dal server in modo formattato

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

    char ip_server[64] = "10.0.70.11";
    char porta_str[16] = "9999";
    int porta = 9999;

    // Pulisci la console
    system("cls");

    // Schermata iniziale elegante e centrata
    set_color(COLOR_TITLE);
    printf("+----------------------------------------------------------+\n");
    printf("|                                                          |\n");
    printf("|               CLIENT TCP/SERIALE STAMPANTE               |\n");
    printf("|                                                          |\n");
    printf("|                    VERSIONE 3.0.0                        |\n");
    printf("|                                                          |\n");
    printf("+----------------------------------------------------------+\n");
    set_color(COLOR_DEFAULT);

    set_color(COLOR_SECTION);
    printf("\nBenvenuto nel client per la comunicazione con la stampante fiscale!\n");
    set_color(COLOR_DEFAULT);
    print_separator();
    set_color(COLOR_INFO);
    printf("  - Server di default: %s:%d\n", ip_server, porta); // default 10.0.70.11
    printf("  - Protocollo: TCP\n");
    set_color(COLOR_DEFAULT);
    print_separator();
    // Richiedi IP
    printf("Inserisci IP server [default: %s]: ", ip_server);
    fgets(ip_server, sizeof(ip_server), stdin);
    ip_server[strcspn(ip_server, "\n")] = 0;
    if (strlen(ip_server) == 0) strcpy(ip_server, "10.0.70.11");
    // Richiedi porta
    printf("Inserisci porta server [default: %d]: ", porta);
    fgets(porta_str, sizeof(porta_str), stdin);
    porta_str[strcspn(porta_str, "\n")] = 0;
    if (strlen(porta_str) > 0) porta = atoi(porta_str);
    // Stampa riepilogo connessione
    printf("[INFO] Connessione a %s:%d\n", ip_server, porta);
    printf("[INFO] Modalita debug attiva: i messaggi di errore saranno dettagliati\n");
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
    printf("- Usa =C1 per attivare la modalita REG\n");
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
        printf(" (esc = esci, multi = invio multiplo, rele = controllo rele, help = mostra comandi)\n");
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

        // Modalità controllo relè
        if (strcmp(message, "rele") == 0) {
            char rele_cmd[256];
            set_color(COLOR_SECTION);
            printf("\n--- Modalita Controllo Rele ---\n");
            set_color(COLOR_DEFAULT);
            printf("Digita 'feed' per attivare l'avanzamento carta.\n");
            printf("Digita 'exit' o premi Invio per tornare al menu principale.\n");
            
            while (1) {
                set_color(COLOR_INPUT);
                printf("rele> ");
                set_color(COLOR_DEFAULT);
                fflush(stdout);

                if (fgets(rele_cmd, sizeof(rele_cmd), stdin) == NULL) {
                    break; // esce in caso di errore o EOF
                }
                rele_cmd[strcspn(rele_cmd, "\n")] = 0; // rimuove newline

                if (strcmp(rele_cmd, "exit") == 0 || strlen(rele_cmd) == 0) {
                    printf("--- Uscita da Modalita Controllo Rele ---\n");
                    break; // esce dal sub-loop
                }

                if (strcmp(rele_cmd, "feed") == 0) {
                    SOCKET sock_temp = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock_temp == INVALID_SOCKET) {
                        set_color(COLOR_ERROR);
                        printf("[X] Errore creazione socket temporaneo: %d\n", WSAGetLastError());
                        set_color(COLOR_DEFAULT);
                        continue;
                    }

                    struct sockaddr_in server_temp;
                    server_temp.sin_addr.s_addr = inet_addr(ip_server);
                    server_temp.sin_family = AF_INET;
                    server_temp.sin_port = htons(porta);

                    if (connect(sock_temp, (struct sockaddr*)&server_temp, sizeof(server_temp)) < 0) {
                        set_color(COLOR_ERROR);
                        printf("[X] Errore connessione temporanea al server: %d\n", WSAGetLastError());
                        closesocket(sock_temp);
                        continue;
                    }

                    char to_send[32];
                    snprintf(to_send, sizeof(to_send), "FEED\r\n");
                    
                    if (send(sock_temp, to_send, (int)strlen(to_send), 0) < 0) {
                        set_color(COLOR_ERROR);
                        printf("[X] Errore durante l'invio del comando 'FEED': %d.\n", WSAGetLastError());
                    } else {
                        // Ora attendiamo una possibile risposta (errore) dal server
                        char server_reply[256];
                        DWORD timeout = 500; // Mezzo secondo di timeout
                        setsockopt(sock_temp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
                        int recv_size = recv(sock_temp, server_reply, sizeof(server_reply) - 1, 0);

                        if (recv_size > 0) {
                            server_reply[recv_size] = '\0';
                            // Controlla se la risposta è un messaggio di successo o un errore
                            if (strncmp(server_reply, "OK:", 3) == 0) {
                                set_color(COLOR_SUCCESS);
                                printf("Risposta dal server: %s", server_reply);
                            } else {
                                set_color(COLOR_ERROR);
                                printf("Errore dal server: %s", server_reply);
                            }
                            set_color(COLOR_DEFAULT);
                        }
                    }
                    
                    closesocket(sock_temp);
                } else {
                    set_color(COLOR_WARNING);
                    printf("Comando non riconosciuto: '%s'. Comandi validi: 'feed', 'exit'.\n", rele_cmd);
                    set_color(COLOR_DEFAULT);
                }
            }
            continue; // Torna all'inizio del loop principale per chiedere un nuovo comando
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
            } // End of for loop for multi_cmds

            // --- Re-establish main connection after multi-commands ---
            printf("[INFO] Re-establishing main connection with server...\n");
            closesocket(sock); // Close the old main socket

            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == INVALID_SOCKET) {
                set_color(COLOR_ERROR);
                printf("[X] Errore creazione socket per riconnessione: %d\n", WSAGetLastError());
                set_color(COLOR_DEFAULT);
                WSACleanup();
                return 1; // Or handle error more gracefully
            }

            // Server address structure is already set up (server.sin_addr, server.sin_family, server.sin_port)
            // Attempt to reconnect
            if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
                set_color(COLOR_ERROR);
                printf("[X] Errore riconnessione al server: %d\n", WSAGetLastError());
                set_color(COLOR_DEFAULT);
                closesocket(sock);
                WSACleanup();
                return 1; // Or handle error more gracefully
            }

            // Set the new socket to non-blocking mode
            u_long mode = 1; // Non-blocking mode
            if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
                set_color(COLOR_ERROR);
                printf("[X] Errore impostazione socket non bloccante dopo riconnessione: %d\n", WSAGetLastError());
                set_color(COLOR_DEFAULT);
                closesocket(sock);
                WSACleanup();
                return 1; // Or handle error more gracefully
            }
            set_color(COLOR_SUCCESS);
            printf("[OK] Riconnesso al server.\n");
            set_color(COLOR_DEFAULT);
            // --- End of re-establishment ---

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
            printf("[X] Errore invio messaggio: %d. Riprova.\n", WSAGetLastError());
            set_color(COLOR_DEFAULT);
            continue; // Skip to next command input if send fails
        }

        // Ricevi la risposta dal server
        int retries = 0;
        const int MAX_RETRIES_RECV = 180; // Aumentato per gestire stampe lunghe
        const int RETRY_DELAY_MS_RECV = 250; // Renamed for clarity

        memset(server_reply, 0, sizeof(server_reply)); // Pulisci il buffer
        recv_size = -1; // Initialize recv_size for this attempt

        for (retries = 0; retries < MAX_RETRIES_RECV; ++retries) {
            recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
            if (recv_size > 0) { // Dati ricevuti correttamente
                break; // Esci dal ciclo di tentativi
            }
            if (recv_size == 0) { // Connessione chiusa dal server
                break; // Esci dal ciclo di tentativi
            }
            // A questo punto, recv_size < 0 (errore)
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                if (retries < MAX_RETRIES_RECV - 1) { // Non attendere all'ultimo tentativo se fallisce
                    // printf("[DEBUG] WSAEWOULDBLOCK su recv, tentativo %d di %d...\n", retries + 1, MAX_RETRIES_RECV);
                    Sleep(RETRY_DELAY_MS_RECV); // Attendi prima del prossimo tentativo
                    continue; // Prova di nuovo recv()
                } else {
                    // L'ultimo tentativo ha comunque dato WSAEWOULDBLOCK, esci per gestire l'errore
                    break;
                }
            } else {
                // Errore diverso da WSAEWOULDBLOCK, non ritentare
                break; // Esci dal ciclo di tentativi
            }
        }

        if (recv_size <= 0) {
            set_color(COLOR_ERROR);
            if (recv_size == 0) {
                printf("[X] Connessione chiusa dal server. Uscita.\n");
            } else {
                printf("[X] Errore ricezione dati: %d. Uscita.\n", WSAGetLastError());
            }
            set_color(COLOR_DEFAULT);
            closesocket(sock);
            sock = INVALID_SOCKET; // Evita riutilizzo
            break; // Esci dal ciclo while(1)
        }

        

        // Stampa la risposta in modo più leggibile
        printf("Risposta dal server:\n");
        set_color(10);
        char campo_dati[1024] = "";
        int parsed_correctly = 0;
        int len = (int)strlen(server_reply);

        // Check for minimal packet length (11 bytes: STX(1)+ADDS(2)+LUNGH_field(3)+PROT-ID(1)+PACK-ID(2)+CHK(1)+ETX(1) + 0 data bytes)
        if (len >= 11 && (unsigned char)server_reply[0] == 0x02 && (unsigned char)server_reply[len-1] == 0x03) {
            // Check PROT-ID (should be 'N' at index 6)
            if (server_reply[6] == 'N') {
                // Ensure LUNGH characters (indices 3,4,5) are digits
                if (isdigit((unsigned char)server_reply[3]) && isdigit((unsigned char)server_reply[4]) && isdigit((unsigned char)server_reply[5])) {
                    int dati_len_from_field = (server_reply[3] - '0') * 100 +
                                              (server_reply[4] - '0') * 10  +
                                              (server_reply[5] - '0');

                    // Validate extracted DATI length against overall packet length
                    // Expected total length = 7 (header up to DATI) + dati_len_from_field + 4 (trailer) = 11 + dati_len_from_field
                    if (dati_len_from_field >= 0 && dati_len_from_field < (int)sizeof(campo_dati) && (11 + dati_len_from_field == len)) {
                        memcpy(campo_dati, &server_reply[7], dati_len_from_field); // DATI starts at index 7
                        campo_dati[dati_len_from_field] = '\0';
                        parsed_correctly = 1;
                    } else {
                        // Optional: Log DATI length mismatch for debugging
                        // printf("[DEBUG] Single cmd: DATI length mismatch. Field: %d, Calculated from total len: %d\n", dati_len_from_field, len - 11);
                    }
                } else {
                    // Optional: Log LUNGH field non-digit for debugging
                    // printf("[DEBUG] Single cmd: LUNGH field contains non-digit characters.\n");
                }
            } else {
                // Optional: Log PROT-ID mismatch for debugging
                // printf("[DEBUG] Single cmd: PROT-ID is not 'N'. Actual: %c\n", server_reply[6]);
            }
        }

        if (!parsed_correctly) {
            // If structured parsing failed, copy the raw reply for pattern matching in stampa_risposta_server
            // printf("[DEBUG] Single cmd: Structured parsing of reply failed. Using raw reply for stampa_risposta_server.\n");
            strncpy(campo_dati, server_reply, sizeof(campo_dati) - 1);
            campo_dati[sizeof(campo_dati) - 1] = '\0';
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