/*
 * File: server.c
 * Descrizione: Server TCP per gestione stampante fiscale
 *              Implementa un protocollo di comunicazione custom con i client
 *              Gestisce concorrenza attraverso mutex
 */

// Disabilita warning per funzioni deprecate di Winsock
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// Definizione dei colori per la console
#define COLOR_DEFAULT 7   // Bianco
#define COLOR_INFO 10     // Verde brillante per info
#define COLOR_ERROR 12    // Rosso brillante per errori
#define COLOR_WARNING 14  // Giallo brillante per warning
#define COLOR_SUCCESS 10  // Verde brillante per successo
#define COLOR_TITLE 11    // Azzurro brillante per titoli
#define COLOR_SECTION 13  // Magenta brillante per sezioni
#define COLOR_DEBUG 15    // Grigio per debug
#define COLOR_INPUT 14    // Giallo per input
#define COLOR_STATUS 11   // Azzurro per stati
#define COLOR_HIGHLIGHT 12  // Rosso brillante per highlight
#define COLOR_SEPARATOR 8  // Grigio scuro per separatori

// Costanti per i colori di background
#define BACKGROUND_WHITE 0xF0
#define BACKGROUND_BLACK 0x00
#define FOREGROUND_YELLOW 0x0E
#define SEPARATOR "------------------------------------------------------------"

// Costanti per la gestione degli errori
#define TIPO_MESSAGGIO_ERRORE 'E'  // Tipo messaggio per errori
#define FAMIGLIA_ERRORE_GENERICO 'G'  // Errore generico
#define FAMIGLIA_ERRORE_BLOCCANTE 'S'  // Errore bloccante
#define FAMIGLIA_ERRORE_CARTA 'P'      // Fine carta

// Prototipo della funzione crea_risposta_errore
int crea_risposta_errore(const char* adds, char famiglia_errore, const char* codice_errore, 
                        const char* messaggio, char* pacchetto, int max_len);

// Definizione costanti configurabili
#define DEFAULT_PORT 9999   // Porta di default
#define MAX_BUFFER 4096     // Dimensione massima buffer
#define MAX_ADDS 3         // Lunghezza massima di adds (2 caratteri + terminatore)
#define BUFFER_CHUNK 128   // Dimensione chunk per buffer
#define MAX_ERROR_COUNT 3   // Numero massimo di errori consecutivi
#define TIMEOUT_MS 30000    // Timeout connessione (30 secondi)

// Inclusione delle librerie necessarie
#include <stdio.h>      // I/O standard
#include <string.h>     // Funzioni stringhe
#include <winsock2.h>   // Socket Windows
#include <time.h>       // Gestione tempo
#include <windows.h>    // Funzioni Windows
#include <stdlib.h>     // Funzioni standard

// Prototipi delle funzioni
DWORD WINAPI client_handler(LPVOID lpParam);
int invia_a_stampante(const char* ip, int porta, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len);
void print_log(const char* msg, int color);

/*
 * Linka automaticamente la libreria ws2_32.lib per MSVC
 * Questa libreria contiene le funzioni di Winsock necessarie
 */
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif


// Flag volatile per gestione graceful shutdown
// Utilizzato per segnalare ai thread di terminare
volatile int server_running = 1;

/*
 * Protocollo di comunicazione
 * ==========================
 * Il protocollo utilizza un formato a pacchetti con checksum per garantire l'integrità dei dati
 * 
 * Struttura del pacchetto:
 * [STX][adds][len][N][dati][pack_id][CHK][ETX]
 * 
 * Campi:
 * - STX: 0x02 (inizio pacchetto)
 * - adds: 2 caratteri identificativi client ("00".."99")
 * - len: 3 cifre, lunghezza campo dati ("008")
 * - N: protocol id (fisso 'N')
 * - dati: campo dati (testo risposta)
 * - pack_id: cifra ciclica 0-9 (qui sempre '1')
 * - CHK: checksum XOR di tutti i byte da adds a pack_id (2 cifre esadecimali ASCII)
 * - ETX: 0x03 (fine pacchetto)
 * 
 * Esempio: 0x02 30 31 30 30 38 4E ...dati... 31 41 42 0x03
 */

/*
 * Comandi supportati dal client
 * ==========================
 * 
 * Comandi di controllo:
 * =K           : CLEAR, resetta tutto lo stato della stampante
 * =k           : Annulla documento commerciale in corso
 * =C0          : Seleziona/deseleziona chiave di Lock
 * =C1          : Seleziona chiave REG (modalità registrazione)
 * =C2          : Seleziona chiave X
 * =C3          : Seleziona chiave Z
 * =C4          : Seleziona chiave PRG
 * =C5          : Seleziona chiave SRV
 * 
 * Comandi di registrazione:
 * =Rxx/$yyyy   : Registra importo (xx=numero reparto, yyyy=importo)
 * =Rxx/$yyyy(nota) : Registra importo con nota opzionale
 * =a           : Annulla ultimo importo registrato
 * 
 * Comandi di totale:
 * =S           : Stampa subtotale
 * =T1          : Stampa totale e chiude in contanti
 * =T2          : Stampa totale e chiude con pagamento Corr. non riscosso
 * =T3          : Stampa totale e chiude con pagamento assegni
 * 
 * Comandi di fidelity:
 * ="/testo     : Inserisce una riga fidelity
 * =c           : Chiude documento commerciale
 * 
 * Comandi di stato:
 * <?s          : Richiede stato (echo)
 * <?d          : Richiede data/ora corrente
 */

// =====================
// === FUNZIONI UTILI ===
// =====================

// Calcola il checksum (XOR di tutti i byte da adds a pack_id incluso)
unsigned char calcola_chk(const char* data, int len) {
    unsigned char bcc = 0;
    for (int i = 0; i < len; i++) {
        bcc ^= (unsigned char)data[i];
    }
    return bcc;
}

// Prototipo funzione per invio alla stampante
int invia_a_stampante(const char* ip, int porta, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len);

// Prototipo funzione per log con timestamp e colore
void print_log(const char* msg, int color);

// Costruisce il pacchetto di risposta secondo il protocollo specificato
int costruisci_pacchetto(const char* adds, const char* dati, int dati_len_logico, char* pacchetto, int max_len) {
    memset(pacchetto, 0, max_len);

    char lungh[4];
    if (dati_len_logico > 999) dati_len_logico = 999;
    snprintf(lungh, sizeof(lungh), "%03d", dati_len_logico);

    int pos = 0;
    pacchetto[pos++] = 0x02; // STX
    memcpy(pacchetto + pos, adds, 2); pos += 2;
    memcpy(pacchetto + pos, lungh, 3); pos += 3;
    pacchetto[pos++] = 'N';
    memcpy(pacchetto + pos, dati, (size_t)dati_len_logico); pos += dati_len_logico;
    pacchetto[pos++] = '1'; // pack_id fisso

    // Calcola il checksum da STX fino a pack_id incluso
    unsigned char chk = 0;
    for (int i = 0; i < pos; i++) {
        chk ^= (unsigned char)pacchetto[i];
    }

    // Scrivi il checksum in ASCII HEX
    snprintf((char*)(pacchetto + pos), 3, "%02X", chk);
    pos += 2;

    pacchetto[pos++] = 0x03; // ETX

    return pos;
}

// =====================
// === STATO STAMPANTE ===
// =====================
// Ogni client ha il suo stato separato (simulazione di una stampante dedicata per ogni connessione)
typedef struct {
    int chiave;           // 0 = lock, 1 = REG, 2 = X, 3 = Z, 4 = PRG, 5 = SRV
    int lock;             // 1 = lock attivo, 0 = no lock
    int totale;           // Totale corrente
    int fidelity_attiva;  // 1 se attiva, 0 se no
    char fidelity1[128];  // Riga fidelity 1
    char fidelity2[128];  // Riga fidelity 2
    int ultimo_importo;   // ultimo importo registrato
    int ultimo_reparto;   // ultimo reparto registrato
    int error_count;      // Conteggio errori consecutivi
    time_t last_command;  // Timestamp dell'ultimo comando
    int session_id;       // ID di sessione univoco per ogni connessione
} StatoStampante;

// =====================
// === LOGICA COMANDI ===
// =====================
// Questa funzione processa ogni comando ricevuto dal client e restituisce la risposta secondo il protocollo
// (Qui puoi implementare la logica di ogni comando, per ora è solo un placeholder)
int crea_risposta(const char* adds, const char* comando, int comando_len, char* pacchetto, int max_len, StatoStampante* stato) {
    // Gestione degli errori consecutivi
    if (stato->error_count >= 3) {
        return crea_risposta_errore(adds, 
                                  FAMIGLIA_ERRORE_BLOCCANTE, 
                                  "0003", 
                                  "Troppi errori consecutivi", 
                                  pacchetto, 
                                  max_len);
    }

    // Resetta il contatore errori
    stato->error_count = 0;
    stato->last_command = time(NULL);
    
    // Verifica se il comando è vuoto
    if (comando_len == 0) {
        return crea_risposta_errore(adds, 
                                  FAMIGLIA_ERRORE_GENERICO, 
                                  "0001", 
                                  "Comando vuoto", 
                                  pacchetto, 
                                  max_len);
    }
    
    // Esempio di gestione di un comando (sostituisci con la tua logica)
    if (strncmp(comando, "=K", 2) == 0) {
        // Comando di reset
        stato->ultimo_importo = 0;
        stato->ultimo_reparto = 0;
        stato->error_count = 0;
        
        // Risposta di successo
        return costruisci_pacchetto(adds, "O|N|0000|Reset completato", 22, pacchetto, max_len);
    } 
    // Aggiungi qui altri comandi...
    else {
        // Comando non riconosciuto
        return crea_risposta_errore(adds, 
                                  FAMIGLIA_ERRORE_GENERICO, 
                                  "0002", 
                                  "Comando non riconosciuto", 
                                  pacchetto, 
                                  max_len);
    }
}

// =====================
// === THREAD CLIENT ===
// =====================
// Ogni client viene gestito da un thread separato, con il suo stato stampante
struct client_args {
    SOCKET sock;
    char adds[3]; // Identificativo client (2 cifre decimali, "00".."99")
};

// Funzione eseguita da ogni thread client
DWORD WINAPI client_handler(LPVOID lpParam) {
    struct client_args* args = (struct client_args*)lpParam;
    SOCKET client_socket = args->sock;
    char adds[3];
    strcpy(adds, args->adds);
    free(args);

    char buffer[2048] = {0};
    int buffer_len = 0;

    // Inizializza lo stato con ID di sessione univoco
    StatoStampante stato = {0};
    stato.session_id = rand() % 1000000;
    print_log("Nuova sessione", COLOR_WARNING);

    // Mostra suggerimenti utili
    printf("\n");


    while (1) {
        // Riceve dati dal client (append al buffer)
        int bytes_received = recv(client_socket, buffer + buffer_len, sizeof(buffer) - buffer_len - 1, 0);
        if (bytes_received <= 0) {
            print_log("Connessione chiusa dal client \n", COLOR_WARNING);
            break;
        }
        buffer_len += bytes_received;
        buffer[buffer_len] = '\0';

        print_log("[DEBUG] Dati ricevuti dal client:", COLOR_DEBUG);
        print_log(buffer, COLOR_DEBUG);

        int start = 0;
        // Processa tutti i comandi completi presenti nel buffer
        while (start < buffer_len) {
            // Cerca newline per delimitare il comando
            char *newline = memchr(buffer + start, '\n', buffer_len - start);
            if (!newline) break;

            // Estrae il comando
            int comando_len = newline - (buffer + start);
            char comando[128];
            strncpy(comando, buffer + start, comando_len);
            comando[comando_len] = '\0';
            // Pulisci caratteri di controllo all'inizio
            int start_idx = 0;
            while (comando_len > 0 && (comando[start_idx] == '\r' || comando[start_idx] == '\n' || (unsigned char)comando[start_idx] == 0x06 || (unsigned char)comando[start_idx] == 0x15 || comando[start_idx] == ' ')) {
                start_idx++;
                comando_len--;
            }
            if (start_idx > 0) memmove(comando, comando + start_idx, comando_len + 1);
            // Pulisci caratteri di controllo/spazi alla fine
            while (comando_len > 0 && (comando[comando_len - 1] == '\r' || comando[comando_len - 1] == '\n' || (unsigned char)comando[comando_len - 1] == 0x06 || (unsigned char)comando[comando_len - 1] == 0x15 || comando[comando_len - 1] == ' ')) {
                comando[comando_len - 1] = '\0';
                comando_len--;
            }

            char debug_msg[256];
            snprintf(debug_msg, sizeof(debug_msg), "[DEBUG] Comando estratto: '%s' (lunghezza: %d)\n", comando, comando_len);
            print_log(debug_msg, COLOR_DEBUG);

            // Costruisce il pacchetto protocollo da inviare alla stampante
            char pacchetto_risposta[2048];
            int pacchetto_len = costruisci_pacchetto(adds, comando, comando_len, pacchetto_risposta, sizeof(pacchetto_risposta));
            snprintf(debug_msg, sizeof(debug_msg), "[DEBUG] Pacchetto da inviare alla stampante (len=%d): '%s'\n", pacchetto_len, pacchetto_risposta);
            print_log(debug_msg, COLOR_DEBUG);
            // Debug protocollo: stampa HEX solo se abilitato
#ifdef DEBUG_PROTOCOL
            printf("[DEBUG] Pacchetto HEX: ");
            for (int i = 0; i < pacchetto_len; i++) printf("%02X ", (unsigned char)pacchetto_risposta[i]);
            printf("\n");
#endif
            if (pacchetto_len > 0) {
                char risposta_stampante[2048] = {0};
                int risposta_len = invia_a_stampante("10.0.70.21", 3000, pacchetto_risposta, pacchetto_len, risposta_stampante, sizeof(risposta_stampante));
                // Debug protocollo: stampa HEX/ASCII risposta stampante solo se abilitato
#ifdef DEBUG_PROTOCOL
                printf("[DEBUG] Risposta HEX dalla stampante: ");
                for (int i = 0; i < risposta_len; i++) printf("%02X ", (unsigned char)risposta_stampante[i]);
                printf("\n");
                printf("[DEBUG] Risposta ASCII dalla stampante: ");
                for (int i = 0; i < risposta_len; i++) {
                    char c = risposta_stampante[i];
                    if (c >= 32 && c <= 126) putchar(c); else putchar('.');
                }
                printf("\n");
#endif
                
                // Se la stampante ha risposto, inoltra la risposta al client
                if (risposta_len > 0) {
                    int sent = send(client_socket, risposta_stampante, risposta_len, 0);
                    snprintf(debug_msg, sizeof(debug_msg), "[DEBUG] Inviati %d bytes al client.", sent);
                    print_log(debug_msg, COLOR_DEBUG);
                } else {
                    // Se la stampante NON ha risposto, invia risposta di errore protocollo al client
                    char risposta_errore[2048];
                    int errore_len = crea_risposta_errore(adds, FAMIGLIA_ERRORE_BLOCCANTE, "0004", "Errore comunicazione con stampante", risposta_errore, sizeof(risposta_errore));
                    int sent = send(client_socket, risposta_errore, errore_len, 0);
                    snprintf(debug_msg, sizeof(debug_msg), "[DEBUG] Inviato errore protocollo al client (%d bytes).", sent);
                    print_log(debug_msg, COLOR_DEBUG);
                }
            } else {
                const char* err_msg = "Errore nella costruzione del pacchetto";
                send(client_socket, err_msg, (int)strlen(err_msg), 0);
            }
            
            // Aggiorna il buffer
            int next_command_start = (newline - buffer) + 1;
            memmove(buffer + start, newline + 1, buffer_len - next_command_start);
            buffer_len -= (comando_len + 1);
            start = next_command_start;
        }
        
        // Se ci sono dati residui, li sposto all'inizio del buffer
        if (start < buffer_len) {
            memmove(buffer, buffer + start, buffer_len - start);
            buffer_len -= start;
        } else {
            // Resetto il buffer se non ci sono dati
            memset(buffer, 0, sizeof(buffer));
            buffer_len = 0;
        }
    }


    // Cleanup
    closesocket(client_socket);
    print_log("Thread client terminato\n", COLOR_WARNING);
    return 0;
}

// =====================
// === FUNZIONI STAMPANTE ===
// =====================
// Funzione per inviare un pacchetto alla stampante fisica e ricevere la risposta
int invia_a_stampante(const char* ip, int porta, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len) {
    if (modalita == MODALITA_WIFI) {
        SOCKET s;
        struct sockaddr_in stampante;
        int risposta_len = -1;

        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return -1;

        stampante.sin_family = AF_INET;
        stampante.sin_addr.s_addr = inet_addr(ip);
        stampante.sin_port = htons(porta);

        if (connect(s, (struct sockaddr*)&stampante, sizeof(stampante)) < 0) {
            closesocket(s);
            return -1;
        }

        if (send(s, pacchetto, pacchetto_len, 0) != pacchetto_len) {
            closesocket(s);
            return -1;
        }

        int total = 0;
        int found_etx = 0;
        while (total < max_risposta_len) {
            int n = recv(s, risposta + total, max_risposta_len - total, 0);
            if (n <= 0) break;
            for (int i = 0; i < n; i++) {
                if ((unsigned char)risposta[total + i] == 0x03) {
                    total += i + 1;
                    found_etx = 1;
                    break;
                }
            }
            if (found_etx) break;
            total += n;
        }
        risposta_len = total;

        closesocket(s);
        return risposta_len;
    } else if (modalita == MODALITA_SERIALE) {
        return invia_a_stampante_seriale(pacchetto, pacchetto_len, risposta, max_risposta_len);
    }
    return -1;
}

// === FUNZIONE PER STAMPA COLORATA IN CONSOLE ===
void print_colored(const char* msg, int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;

    // Salva il colore corrente
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;

    // Imposta il nuovo colore
    SetConsoleTextAttribute(hConsole, color);

    // Stampa il messaggio
    printf("%s", msg);

    // Ripristina il colore originale
    SetConsoleTextAttribute(hConsole, saved_attributes);
}

// =====================
// === GESTIONE ERRORI ===
// =====================

/**
 * Crea una risposta di errore standardizzata secondo il protocollo
 * @param adds Identificativo client (2 caratteri)
 * @param famiglia_errore Codice famiglia errore ('G', 'S', 'P')
 * @param codice_errore Codice errore (4 caratteri numerici)
 * @param messaggio Messaggio descrittivo dell'errore
 * @param pacchetto Buffer dove salvare il pacchetto di risposta
 * @param max_len Dimensione massima del buffer
 * @return Lunghezza del pacchetto creato, o -1 in caso di errore
 */
int crea_risposta_errore(const char* adds, char famiglia_errore, const char* codice_errore, const char* messaggio, char* pacchetto, int max_len) {
    // Buffer per i dati da inviare
    char buffer_dati[1024];
    
    // Formatta i dati secondo il protocollo: TIPO|FAMIGLIA|CODICE|MESSAGGIO
    int dati_len = snprintf(buffer_dati, sizeof(buffer_dati), "%c|%c|%s|%s", 
                          TIPO_MESSAGGIO_ERRORE, 
                          famiglia_errore,
                          codice_errore,
                          messaggio);
    
    if (dati_len < 0 || dati_len >= (int)sizeof(buffer_dati)) {
        return -1; // Errore di formattazione o buffer overflow
    }
    
    // Usa la funzione esistente per costruire il pacchetto con checksum
    return costruisci_pacchetto(adds, buffer_dati, dati_len, pacchetto, max_len);
}

// === FUNZIONE PER LOG CON TIMESTAMP ===
// Prototipo della funzione print_separator
void print_separator();

void print_log(const char* msg, int color) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "[%H:%M:%S] ", t);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;

    // Imposta il colore del testo
    SetConsoleTextAttribute(hConsole, color);
    printf("%s%s", timebuf, msg);
    SetConsoleTextAttribute(hConsole, saved_attributes);
}

// Stampa un separatore
void print_separator() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;

    SetConsoleTextAttribute(hConsole, COLOR_SEPARATOR);
    printf("\n%s\n", SEPARATOR);
    SetConsoleTextAttribute(hConsole, saved_attributes);
}

// =====================
// === MAIN SERVER ===
// =====================
// Avvia il server TCP, accetta connessioni e crea un thread per ogni client
int main() {
    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c;
    int port;

    // Banner di benvenuto colorato
    print_colored("+----------------------------------------------------------+\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    print_colored("|                ", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    print_colored("SERVER TCP - CONSOLE v2.0.0", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    print_colored("               |\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    print_colored("+----------------------------------------------------------+\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);

    // Chiedi la porta all'utente
    printf("\nInserisci la porta su cui far andare il server [default: %d]: ", DEFAULT_PORT);
    char input[6];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;  // Rimuovi il newline

    if (strlen(input) > 0) {
        port = atoi(input);
        if (port <= 0 || port > 65535) {
            char msg[100];
            snprintf(msg, sizeof(msg), "Porta non valida, utilizzo porta di default %d", DEFAULT_PORT);
            print_log(msg, COLOR_WARNING);
            port = DEFAULT_PORT;
        }
    } else {
        port = DEFAULT_PORT;
    }

    char msg[100];
    snprintf(msg, sizeof(msg), "Porta selezionata: %d\n", port);
    print_log(msg, COLOR_INFO);

    printf("Inizializzo Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Errore Winsock: %d\n", WSAGetLastError());
        return 1;
    }

    // Crea socket TCP
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Errore creazione socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Configura struttura server (IPv4, qualsiasi IP, porta selezionata)
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    // Associa socket all'indirizzo e porta
    if (bind(server_socket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Errore bind: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Mette la socket in ascolto
    if (listen(server_socket, 3) == SOCKET_ERROR) {
        printf("Errore listen: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Server in ascolto sulla porta %d...\n", ntohs(server.sin_port));

    // Inizializza la struttura client
    c = sizeof(struct sockaddr_in);

    // Ciclo principale: accetta nuove connessioni
    while (server_running) {
        print_log("In attesa di connessione...\n", FOREGROUND_INTENSITY | FOREGROUND_GREEN);

        client_socket = accept(server_socket, (struct sockaddr*)&client, &c);
        if (!server_running) break; // Esci subito se è stato richiesto lo shutdown

        if (client_socket == INVALID_SOCKET) {
            if (server_running) // Solo logga errore se non è stato richiesto lo shutdown
                print_log("Errore accept!\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            continue;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "Connessione accettata da %s:%d\n",
            inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        print_log(buf, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

        // Calcola adds in base all'IP del client (ultimi 2 numeri decimali dell'IP)
        unsigned int ip = ntohl(client.sin_addr.s_addr);
        struct client_args* args = malloc(sizeof(struct client_args));
        args->sock = client_socket;
        strcpy(args->adds, "01"); // Forza adds a "01" per tutti i client (puoi personalizzare)

        // Crea un thread per gestire il client
        HANDLE hThread = CreateThread(
            NULL, 0, client_handler, (LPVOID)args, 0, NULL
        );
        if (hThread != NULL) {
            CloseHandle(hThread);
        } else {
            print_log("Errore creazione thread client.\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            closesocket(client_socket);
            free(args);
        }
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}

// === MODALITÀ DI COMUNICAZIONE ===
typedef enum { MODALITA_WIFI, MODALITA_SERIALE } ModalitaComunicazione;
ModalitaComunicazione modalita = MODALITA_WIFI;

// === HANDLE PER LA PORTA SERIALE ===
HANDLE hSerial = INVALID_HANDLE_VALUE;

// === FUNZIONI SERIALI ===
int apri_seriale(const char* porta) {
    hSerial = CreateFileA(porta, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) return 0;

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) return 0;
    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) return 0;

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return 1;
}

void chiudi_seriale() {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
}

int invia_a_stampante_seriale(const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len) {
    DWORD bytes_written, bytes_read;
    if (!WriteFile(hSerial, pacchetto, pacchetto_len, &bytes_written, NULL) || bytes_written != (DWORD)pacchetto_len)
        return -1;

    // Riceve fino a ETX (0x03) o fine buffer
    int total = 0;
    int found_etx = 0;
    while (total < max_risposta_len) {
        if (!ReadFile(hSerial, risposta + total, 1, &bytes_read, NULL) || bytes_read == 0) break;
        if ((unsigned char)risposta[total] == 0x03) {
            total++;
            found_etx = 1;
            break;
        }
        total++;
    }
    return found_etx ? total : -1;
}

// Modifica la funzione invia_a_stampante per gestire entrambe le modalità
int invia_a_stampante(const char* ip, int porta, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len) {
    if (modalita == MODALITA_WIFI) {
        SOCKET s;
        struct sockaddr_in stampante;
        int risposta_len = -1;

        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return -1;

        stampante.sin_family = AF_INET;
        stampante.sin_addr.s_addr = inet_addr(ip);
        stampante.sin_port = htons(porta);

        if (connect(s, (struct sockaddr*)&stampante, sizeof(stampante)) < 0) {
            closesocket(s);
            return -1;
        }

        if (send(s, pacchetto, pacchetto_len, 0) != pacchetto_len) {
            closesocket(s);
            return -1;
        }

        int total = 0;
        int found_etx = 0;
        while (total < max_risposta_len) {
            int n = recv(s, risposta + total, max_risposta_len - total, 0);
            if (n <= 0) break;
            for (int i = 0; i < n; i++) {
                if ((unsigned char)risposta[total + i] == 0x03) {
                    total += i + 1;
                    found_etx = 1;
                    break;
                }
            }
            if (found_etx) break;
            total += n;
        }
        risposta_len = total;

        closesocket(s);
        return risposta_len;
    } else if (modalita == MODALITA_SERIALE) {
        return invia_a_stampante_seriale(pacchetto, pacchetto_len, risposta, max_risposta_len);
    }
    return -1;
}

// Nel main, chiedi la modalità di comunicazione e la porta seriale se serve
int main() {
    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c;
    int port;

    // Banner di benvenuto colorato
    print_colored("+----------------------------------------------------------+\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    print_colored("|                ", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    print_colored("SERVER TCP - CONSOLE v2.0.0", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    print_colored("               |\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    print_colored("+----------------------------------------------------------+\n", FOREGROUND_BLUE | FOREGROUND_INTENSITY);

    // Chiedi la porta all'utente
    printf("\nInserisci la porta su cui far andare il server [default: %d]: ", DEFAULT_PORT);
    char input[6];
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;  // Rimuovi il newline

    if (strlen(input) > 0) {
        port = atoi(input);
        if (port <= 0 || port > 65535) {
            char msg[100];
            snprintf(msg, sizeof(msg), "Porta non valida, utilizzo porta di default %d", DEFAULT_PORT);
            print_log(msg, COLOR_WARNING);
            port = DEFAULT_PORT;
        }
    } else {
        port = DEFAULT_PORT;
    }

    char msg[100];
    snprintf(msg, sizeof(msg), "Porta selezionata: %d\n", port);
    print_log(msg, COLOR_INFO);

    printf("Inizializzo Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Errore Winsock: %d\n", WSAGetLastError());
        return 1;
    }

    // Crea socket TCP
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Errore creazione socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Configura struttura server (IPv4, qualsiasi IP, porta selezionata)
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    // Associa socket all'indirizzo e porta
    if (bind(server_socket, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Errore bind: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Mette la socket in ascolto
    if (listen(server_socket, 3) == SOCKET_ERROR) {
        printf("Errore listen: %d\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Server in ascolto sulla porta %d...\n", ntohs(server.sin_port));

    // Inizializza la struttura client
    c = sizeof(struct sockaddr_in);

    // Ciclo principale: accetta nuove connessioni
    while (server_running) {
        print_log("In attesa di connessione...\n", FOREGROUND_INTENSITY | FOREGROUND_GREEN);

        client_socket = accept(server_socket, (struct sockaddr*)&client, &c);
        if (!server_running) break; // Esci subito se è stato richiesto lo shutdown

        if (client_socket == INVALID_SOCKET) {
            if (server_running) // Solo logga errore se non è stato richiesto lo shutdown
                print_log("Errore accept!\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            continue;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "Connessione accettata da %s:%d\n",
            inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        print_log(buf, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);

        // Calcola adds in base all'IP del client (ultimi 2 numeri decimali dell'IP)
        unsigned int ip = ntohl(client.sin_addr.s_addr);
        struct client_args* args = malloc(sizeof(struct client_args));
        args->sock = client_socket;
        strcpy(args->adds, "01"); // Forza adds a "01" per tutti i client (puoi personalizzare)

        // Crea un thread per gestire il client
        HANDLE hThread = CreateThread(
            NULL, 0, client_handler, (LPVOID)args, 0, NULL
        );
        if (hThread != NULL) {
            CloseHandle(hThread);
        } else {
            print_log("Errore creazione thread client.\n", FOREGROUND_RED | FOREGROUND_INTENSITY);
            closesocket(client_socket);
            free(args);
        }
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}