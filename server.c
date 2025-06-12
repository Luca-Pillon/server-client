/*
 * File: server.c
 * Descrizione: Server TCP per gestione stampante fiscale
 *              Implementa un protocollo di comunicazione custom con i client
 *              Gestisce concorrenza attraverso mutex
 */

// Definisce la versione minima di Windows API per compatibilità (es. per inet_ntop)
#define _WIN32_WINNT 0x0600
#include <stdlib.h> // Per system()
#include "relay_control.h"  // Per il controllo del relè

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
#define APP_COLOR_HIGHLIGHT 12  // Rosso brillante per highlight
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
#define DEFAULT_PRINTER_IP "10.0.70.11"
#define DEFAULT_PRINTER_PORT 3000

// Inclusione delle librerie necessarie
#include <stdio.h>      // I/O standard
#include <string.h>     // Funzioni stringhe
#include <winsock2.h>   // Socket Windows
#include <windows.h>    // Funzioni Windows (necessario per API seriali)
#include <ws2tcpip.h>   // Per inet_pton (necessario per alcune versioni MinGW/GCC)
#include <time.h>       // Gestione tempo
#include <WinError.h>   // Per ERROR_OPERATION_ABORTED etc.
#include <stdlib.h>     // Funzioni standard
#include "relay_control.h"  // Inclusione del modulo relè

// === DEFINIZIONI PER MODALITÀ DI COMUNICAZIONE ===
typedef enum {
    MODE_UNINITIALIZED = 0,
    MODE_TCP_IP,
    MODE_SERIAL
} CommunicationMode;

// Variabili globali per la configurazione del server e della stampante
CommunicationMode g_server_listen_mode = MODE_UNINITIALIZED;
char g_server_listen_serial_port_name[20]; // Es. "COM1"
int g_server_listen_tcp_port = DEFAULT_PORT;

CommunicationMode g_printer_connection_mode = MODE_UNINITIALIZED;
char g_printer_conn_ip_address[16];      // Es. "192.168.1.100"
int g_printer_conn_tcp_port;
char g_printer_conn_serial_port_name[20]; // Es. "COM2"
HANDLE h_printer_comm_port = INVALID_HANDLE_VALUE; // Handle per la porta seriale della stampante

BOOL g_relay_module_enabled = FALSE; // Flag per indicare se il modulo relè è stato abilitato e inizializzato correttamente

// Parametri seriali stampante (fissi come da richiesta)
#define PRINTER_BAUD_RATE 9600
#define PRINTER_PARITY NOPARITY
#define PRINTER_STOP_BITS ONESTOPBIT
#define PRINTER_BYTE_SIZE 8

// Prototipi delle funzioni
DWORD WINAPI tcp_client_handler(LPVOID lpParam); // Rinominata da client_handler
DWORD WINAPI serial_client_handler(LPVOID lpParam); // lpParam sarà l'handle della porta seriale del client

// Funzioni per l'invio alla stampante
int invia_a_stampante_dispatcher(const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len);
int invia_a_stampante_tcp(const char* ip, int porta, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len);
int invia_a_stampante_seriale(HANDLE hComm, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len);

void print_log(const char* msg, int color);

// Prototipi per la gestione TCP e Seriale del server
void start_tcp_server(int port);
void start_serial_server(const char* port_name);
BOOL configure_serial_port(const char* port_name, HANDLE* hSerial, int baud_rate, BYTE parity, BYTE stop_bits, BYTE byte_size, BOOL for_printer_comm);
void close_serial_port_handle(HANDLE* hComm); // Funzione helper per chiudere la porta seriale
int read_from_serial_port(HANDLE hComm, char* buffer, int buffer_len); // Funzione helper per leggere dalla seriale
int write_to_serial_port(HANDLE hComm, const char* data, int data_len); // Funzione helper per scrivere su seriale

// Parametri per la comunicazione seriale (RS232/UART)
#define SERIAL_BAUD_RATE 9600
#define SERIAL_BYTE_SIZE 8
#define SERIAL_PARITY NOPARITY
#define SERIAL_STOP_BITS ONESTOPBIT

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

// La funzione invia_a_stampante originale è ora invia_a_stampante_tcp.
// invia_a_stampante_dispatcher deciderà quale usare.

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

// Struttura per passare argomenti al thread client seriale
struct serial_client_args {
    HANDLE hClientSerial; // Handle alla porta seriale del client
    char adds[MAX_ADDS];  // Identificativo client (es. "S0")
};

struct client_args {
    SOCKET sock;
    char adds[3]; // Identificativo client (2 cifre decimali, "00".."99")
};

// Funzione eseguita da ogni thread client TCP
DWORD WINAPI tcp_client_handler(LPVOID lpParam) {
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

        print_log("[DEBUG] Dati ricevuti dal client:\n", COLOR_DEBUG);
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

            // Qui puoi aggiungere comandi speciali che non vanno alla stampante
            if (strncmp(comando, "FEED", 4) == 0) {
                if (g_relay_module_enabled) {
                    print_log("Comando FEED ricevuto. Attivazione rele per avanzamento carta...", COLOR_INFO);
                    pulse_relay(200); // Simula la pressione di un pulsante per 200ms
                } else {
                    print_log("Comando FEED ricevuto, ma modulo rele disabilitato. Comando ignorato.", COLOR_WARNING);
                    char* error_msg = "ERRORE: Modulo rele non abilitato o non disponibile.\r\n";
                    send(client_socket, error_msg, strlen(error_msg), 0);
                }
                start += comando_len + 1; // Avanza al prossimo comando
                continue;
            }

            char pacchetto_risposta[2048];
            int pacchetto_len = costruisci_pacchetto(adds, comando, comando_len, pacchetto_risposta, sizeof(pacchetto_risposta));
            
            if (pacchetto_len > 0) {
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
                    int risposta_len = invia_a_stampante_dispatcher(pacchetto_risposta, pacchetto_len, risposta_stampante, sizeof(risposta_stampante));
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
                        snprintf(debug_msg, sizeof(debug_msg), "[DEBUG] Inviati %d bytes al client.\n", sent);
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
        
        // Se processed_upto == 0 e c'erano dati, significa che non è stato trovato un newline.
        // Il buffer si riempirà finché non arriva un newline o si esaurisce lo spazio.
        if (buffer_len == sizeof(buffer) -1) {
             print_log("Buffer ricezione client pieno e nessun newline. Reset buffer.", COLOR_WARNING);
             buffer_len = 0; // Evita overflow, scarta dati vecchi
             buffer[0] = '\0';
        }
    }


    // Cleanup
    closesocket(client_socket);
    print_log("Thread client terminato\n", COLOR_WARNING);
    return 0;
}

// Funzione eseguita da ogni thread client Seriale
DWORD WINAPI serial_client_handler(LPVOID lpParam) {
    struct serial_client_args* args = (struct serial_client_args*)lpParam;
    HANDLE hClientSerial = args->hClientSerial;
    char adds[MAX_ADDS];
    strncpy(adds, args->adds, MAX_ADDS);
    adds[MAX_ADDS - 1] = '\0';
    free(args); // Libera la memoria allocata per gli argomenti

    char recv_buffer[MAX_BUFFER] = {0};
    int recv_buffer_len = 0;
    DWORD bytes_read;

    StatoStampante stato = {0};
    stato.session_id = rand() % 1000000; // ID sessione semplice
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Nuova sessione seriale per client %s su handle %p", adds, hClientSerial);
    print_log(log_msg, COLOR_INFO);

    // Imposta timeout per ReadFile sulla porta seriale del client
    // Questo è importante per non bloccare indefinitamente se il client non invia nulla
    // o per gestire la chiusura della connessione.
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50; // Max time between two chars, ms
    timeouts.ReadTotalTimeoutMultiplier = 10; // Multiplier per byte
    timeouts.ReadTotalTimeoutConstant = 1000; // Constant timeout, ms (e.g., 1 sec total for a read)
    // Write timeouts non sono strettamente necessari qui se ci aspettiamo risposte rapide
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 1000;
    if (!SetCommTimeouts(hClientSerial, &timeouts)) {
        snprintf(log_msg, sizeof(log_msg), "Errore impostazione timeouts per client seriale %s. Errore: %lu", adds, GetLastError());
        print_log(log_msg, COLOR_ERROR);
        // Non chiudiamo l'handle qui, lo gestirà start_serial_server
        return 1; // Termina il thread
    }

    while (server_running) {
        // Legge dati dal client seriale (append al buffer)
        // ReadFile con timeout leggerà quello che c'è, o tornerà dopo il timeout.
        if (!ReadFile(hClientSerial, recv_buffer + recv_buffer_len, sizeof(recv_buffer) - recv_buffer_len - 1, &bytes_read, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_OPERATION_ABORTED || error == ERROR_INVALID_HANDLE) { // Porta chiusa o errore grave
                 snprintf(log_msg, sizeof(log_msg), "Errore lettura da client seriale %s (handle %p) o porta chiusa. Errore: %lu. Thread termina.", adds, hClientSerial, error);
                 print_log(log_msg, COLOR_ERROR);
                 break;
            }
            // Altri errori potrebbero essere non fatali o legati ai timeout, che sono gestiti da bytes_read == 0
            snprintf(log_msg, sizeof(log_msg), "[DEBUG] Errore ReadFile da client seriale %s. Errore: %lu", adds, error);
            print_log(log_msg, COLOR_DEBUG); 
            // Potrebbe essere un timeout, continuiamo il ciclo per vedere se server_running è cambiato
            Sleep(100); // Breve pausa in caso di errore di lettura non fatale
            continue;
        }

        if (bytes_read == 0) { // Timeout o nessuna data
            // Se server_running è ancora true, semplicemente non c'erano dati.
            // Se il client si disconnette fisicamente, ReadFile potrebbe continuare a tornare con 0 bytes_read
            // o potrebbe dare un errore gestito sopra.
            Sleep(50); // Attesa breve prima di riprovare
            continue;
        }

        recv_buffer_len += bytes_read;
        recv_buffer[recv_buffer_len] = '\0';

        snprintf(log_msg, sizeof(log_msg), "[DEBUG] Dati ricevuti da client seriale %s (%d bytes): %.*s", adds, bytes_read, bytes_read, recv_buffer + (recv_buffer_len - bytes_read));
        print_log(log_msg, COLOR_DEBUG);

        int processed_upto = 0;
        // Processa tutti i comandi completi (newline-terminated) presenti nel buffer
        while (processed_upto < recv_buffer_len) {
            char* newline = strchr(recv_buffer + processed_upto, '\n');
            if (!newline) {
                // Comando non completo, attendi altri dati (o sposta i dati parziali all'inizio del buffer)
                break;
            }

            int comando_len = newline - (recv_buffer + processed_upto);
            char comando[MAX_BUFFER]; // Abbastanza grande per un comando
            strncpy(comando, recv_buffer + processed_upto, comando_len);
            comando[comando_len] = '\0';

            // Pulisce eventuali CR prima del LF
            if (comando_len > 0 && comando[comando_len - 1] == '\r') {
                comando[comando_len - 1] = '\0';
                comando_len--;
            }
            
            // Avanza il puntatore di inizio per il prossimo comando nel buffer
            processed_upto += comando_len + 1; // +1 per il newline
            printf("\n");
            snprintf(log_msg, sizeof(log_msg), "[DEBUG] Comando estratto da client seriale %s: '%s' (len: %d)\n", adds, comando, comando_len);
            print_log(log_msg, COLOR_DEBUG);

            if (comando_len == 0) { // Comando vuoto dopo pulizia, ignora
                print_log("[DEBUG] Comando vuoto ricevuto da client seriale, ignorato.\n", COLOR_DEBUG);
                continue;
            }

            char pacchetto_stampante[MAX_BUFFER];
            int pacchetto_len = costruisci_pacchetto(adds, comando, comando_len, pacchetto_stampante, sizeof(pacchetto_stampante));
            
            if (pacchetto_len > 0) {
                snprintf(log_msg, sizeof(log_msg), "[DEBUG] Pacchetto per stampante da client seriale %s (len=%d): %.*s", adds, pacchetto_len, pacchetto_len, pacchetto_stampante);
                print_log(log_msg, COLOR_DEBUG);

                char risposta_stampante[MAX_BUFFER] = {0};
                int len_risposta_stampante = invia_a_stampante_dispatcher(pacchetto_stampante, pacchetto_len, risposta_stampante, sizeof(risposta_stampante));

                if (len_risposta_stampante > 0) {
                    snprintf(log_msg, sizeof(log_msg), "[DEBUG] Risposta da stampante per client seriale %s (%d bytes): %.*s", adds, len_risposta_stampante, len_risposta_stampante, risposta_stampante);
                    print_log(log_msg, COLOR_DEBUG);
                    int bytes_written = write_to_serial_port(hClientSerial, risposta_stampante, len_risposta_stampante);
                    if (bytes_written < 0 || bytes_written != len_risposta_stampante) {
                        snprintf(log_msg, sizeof(log_msg), "Errore scrittura risposta a client seriale %s.", adds);
                        print_log(log_msg, COLOR_ERROR);
                        // Potrebbe essere necessario chiudere la connessione qui se l'errore è grave
                    }
                } else {
                    char risposta_errore[MAX_BUFFER];
                    int errore_len = crea_risposta_errore(adds, FAMIGLIA_ERRORE_BLOCCANTE, "0004", "Errore comunicazione con stampante", risposta_errore, sizeof(risposta_errore));
                    snprintf(log_msg, sizeof(log_msg), "[DEBUG] Invio errore protocollo a client seriale %s (%d bytes).", adds, errore_len);
                    print_log(log_msg, COLOR_DEBUG);
                    write_to_serial_port(hClientSerial, risposta_errore, errore_len);
                }
            } else {
                char risposta_errore[MAX_BUFFER];
                int errore_len = crea_risposta_errore(adds, FAMIGLIA_ERRORE_GENERICO, "0005", "Errore costruzione pacchetto interno", risposta_errore, sizeof(risposta_errore));
                snprintf(log_msg, sizeof(log_msg), "[DEBUG] Errore costruzione pacchetto, invio errore a client seriale %s.", adds);
                print_log(log_msg, COLOR_WARNING);
                write_to_serial_port(hClientSerial, risposta_errore, errore_len);
            }
        }

        // Sposta i dati non processati (comando parziale) all'inizio del buffer
        if (processed_upto > 0 && processed_upto < recv_buffer_len) {
            memmove(recv_buffer, recv_buffer + processed_upto, recv_buffer_len - processed_upto);
            recv_buffer_len -= processed_upto;
            recv_buffer[recv_buffer_len] = '\0'; // Null-terminate again
        } else if (processed_upto == recv_buffer_len) {
            // Tutto il buffer è stato processato
            recv_buffer_len = 0;
            recv_buffer[0] = '\0';
        }
        // Se processed_upto == 0 e c'erano dati, significa che non è stato trovato un newline.
        // Il buffer si riempirà finché non arriva un newline o si esaurisce lo spazio.
        if (recv_buffer_len == sizeof(recv_buffer) -1) {
             print_log("Buffer ricezione client seriale pieno e nessun newline. Reset buffer.", COLOR_WARNING);
             recv_buffer_len = 0; // Evita overflow, scarta dati vecchi
             recv_buffer[0] = '\0';
        }
    }

    snprintf(log_msg, sizeof(log_msg), "Thread client seriale %s terminato (handle %p).", adds, hClientSerial);
    print_log(log_msg, COLOR_WARNING);
    // La chiusura di hClientSerial è responsabilità di start_serial_server o main
    // in base a come viene gestito il ciclo di vita della porta seriale del client.
    return 0;
}

// =====================
// === FUNZIONI STAMPANTE ===
// =====================
// Funzione per inviare un pacchetto alla stampante fisica e ricevere la risposta
int invia_a_stampante_dispatcher(const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len) {
    if (g_printer_connection_mode == MODE_TCP_IP) {
        return invia_a_stampante_tcp(g_printer_conn_ip_address, g_printer_conn_tcp_port, pacchetto, pacchetto_len, risposta, max_risposta_len);
    } else if (g_printer_connection_mode == MODE_SERIAL) {
        if (h_printer_comm_port == INVALID_HANDLE_VALUE) {
            print_log("Errore: Handle porta seriale stampante non valido. Tentativo di riapertura...", COLOR_ERROR);
            if (!configure_serial_port(g_printer_conn_serial_port_name, &h_printer_comm_port, PRINTER_BAUD_RATE, PRINTER_PARITY, PRINTER_STOP_BITS, PRINTER_BYTE_SIZE, TRUE)) {
                print_log("Fallito tentativo di riaprire la porta seriale della stampante.", COLOR_ERROR);
                return -1; 
            }
            print_log("Porta seriale stampante riaperta con successo.", COLOR_INFO);
        }
        return invia_a_stampante_seriale(h_printer_comm_port, pacchetto, pacchetto_len, risposta, max_risposta_len);
    } else {
        print_log("Errore: Modalita' di connessione stampante non configurata.", COLOR_ERROR);
        return -1;
    }
}

// Funzione per inviare un pacchetto alla stampante fisica via TCP/IP e ricevere la risposta
// (Questa era la vecchia funzione invia_a_stampante)
int invia_a_stampante_tcp(const char* ip, int porta, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len) {
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

    // Riceve la risposta fino a ETX (0x03) o fine buffer
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
}

// Funzione per inviare un pacchetto alla stampante fisica via Seriale e ricevere la risposta
int invia_a_stampante_seriale(HANDLE hComm, const char* pacchetto, int pacchetto_len, char* risposta, int max_risposta_len) {
    if (hComm == INVALID_HANDLE_VALUE) {
        print_log("Errore: Handle porta seriale stampante non valido per invio.", COLOR_ERROR);
        return -1;
    }

    print_log("Invio dati alla stampante seriale...\n", COLOR_DEBUG);
    int bytes_written = write_to_serial_port(hComm, pacchetto, pacchetto_len);
    if (bytes_written < 0) {
        return -2; // Errore già loggato
    }
    if (bytes_written != pacchetto_len) {
        print_log("Errore: non tutti i byte sono stati scritti sulla seriale della stampante.", COLOR_WARNING);
    }

    print_log("Attesa risposta dalla stampante seriale...\n", COLOR_DEBUG);
    memset(risposta, 0, max_risposta_len);
    
    // Logica di lettura della risposta dalla stampante seriale.
    // Il protocollo prevede STX all'inizio e ETX alla fine.
    // Bisogna leggere fino a ETX o timeout/errore.
    int total_bytes_read = 0;
    BOOL etx_found = FALSE;
    DWORD start_time = GetTickCount(); // Per timeout manuale sulla ricezione completa del pacchetto

    while (total_bytes_read < max_risposta_len -1 && !etx_found) {
        if (GetTickCount() - start_time > TIMEOUT_MS) { // Timeout generale per la risposta completa
            print_log("Timeout generale attesa risposta completa da stampante seriale.", COLOR_WARNING);
            break;
        }
        char temp_char;
        int bytes_chunk_read = read_from_serial_port(hComm, &temp_char, 1);

        if (bytes_chunk_read < 0) { // Errore di lettura
            print_log("Errore lettura da seriale stampante durante attesa risposta.", COLOR_ERROR);
            return -3;
        }
        if (bytes_chunk_read == 0) { // Timeout sulla singola lettura (normale se ReadIntervalTimeout è impostato)
            Sleep(10); // Piccola pausa per non ciclare troppo velocemente
            continue;
        }

        risposta[total_bytes_read++] = temp_char;
        if (temp_char == 0x03) { // ETX
            etx_found = TRUE;
        }
    }
    risposta[total_bytes_read] = '\0';

    if (!etx_found && total_bytes_read > 0) {
        print_log("Risposta da stampante seriale ricevuta ma senza ETX finale o buffer pieno.", COLOR_WARNING);
    } else if (total_bytes_read == 0 && !etx_found) {
        print_log("Nessuna risposta o risposta vuota dalla stampante seriale.", COLOR_WARNING);
    }
    
    char log_resp[200];
    snprintf(log_resp, sizeof(log_resp), "[DEBUG] Risposta da stampante seriale (%d bytes): %.*s\n", total_bytes_read, total_bytes_read, risposta);
    print_log(log_resp, COLOR_DEBUG);

    return total_bytes_read;
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

// ==================================
// === CONTROLLO STAMPANTE VIA RELÈ ===
// ==================================
void controlla_stampante(int accendi) {
    if (!relay_is_ready()) {
        print_log("Impossibile controllare la stampante: modulo rele non disponibile.", COLOR_ERROR);
        return;
    }

    if (accendi) {
        print_log("Accensione stampante tramite rele...\n", COLOR_INFO);
        relay_on();
        print_log("Rele attivato. La stampante dovrebbe essere accesa.\n", COLOR_SUCCESS);
    } else {
        print_log("Spegnimento stampante tramite rele...\n", COLOR_INFO);
        relay_off();
        print_log("Rele disattivato. La stampante dovrebbe essere spenta.\n", COLOR_SUCCESS);
    }
}

// =====================
// === GESTIONE ERRORI ===
// =====================
// Variabile globale per controllare lo stato del server
volatile BOOL is_running = TRUE;
SOCKET listen_socket = INVALID_SOCKET; // Socket di ascolto globale

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

// Funzione per pulire il buffer di input (stdin)
void clear_stdin_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// =====================
// === FUNZIONI DI AVVIO SERVER ===
// =====================
DWORD WINAPI server_thread_func(LPVOID lpParam) {
    int port = (int)(INT_PTR)lpParam;
    start_tcp_server(port);
    return 0;
}

void start_tcp_server(int port) {
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Tentativo di avviare il server TCP sulla porta %d...\n", port);
    print_log(log_msg, COLOR_INFO);

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        snprintf(log_msg, sizeof(log_msg), "WSAStartup fallito: %d. Server TCP non avviato.", iResult);
        print_log(log_msg, COLOR_ERROR);
        return;
    }

    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        snprintf(log_msg, sizeof(log_msg), "Creazione socket fallita: %ld. Server TCP non avviato.", WSAGetLastError());
        print_log(log_msg, COLOR_ERROR);
        WSACleanup();
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        snprintf(log_msg, sizeof(log_msg), "Bind fallito: %ld. Server TCP non avviato.", WSAGetLastError());
        print_log(log_msg, COLOR_ERROR);
        closesocket(listen_socket);
        WSACleanup();
        return;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        snprintf(log_msg, sizeof(log_msg), "Listen fallito: %ld. Server TCP non avviato.", WSAGetLastError());
        print_log(log_msg, COLOR_ERROR);
        closesocket(listen_socket);
        WSACleanup();
        return;
    }

    snprintf(log_msg, sizeof(log_msg), "Server TCP in ascolto sulla porta %d.\nIn attesa di connessioni client...\n", port);
    print_log(log_msg, COLOR_INFO);

    SOCKET client_socket;
    struct sockaddr_in client_addr;
    int client_addr_size = sizeof(client_addr);
    static int tcp_client_id_counter = 0;
    HANDLE h_thread;

    while (is_running) {
        client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_socket == INVALID_SOCKET) {
            if (!is_running) { // Errore atteso perché abbiamo chiuso il socket
                print_log("accept interrotto a seguito di chiusura server.", COLOR_INFO);
                break; // Usciamo dal loop
            } else { // Errore inaspettato
                snprintf(log_msg, sizeof(log_msg), "accept fallito con errore: %d", WSAGetLastError());
                print_log(log_msg, COLOR_ERROR);
                continue; // Riprova
            }
        }

        char* client_ip_str = inet_ntoa(client_addr.sin_addr); // inet_ntoa è più vecchio e IPv4-only, ma più portabile su vecchi MinGW
        // ATTENZIONE: inet_ntoa non è thread-safe se chiamato da più thread contemporaneamente senza protezione,
        // ma per il logging qui, dove la stringa viene usata subito, il rischio è basso.
        snprintf(log_msg, sizeof(log_msg), "Nuova connessione TCP accettata da %s:%d\n", client_ip_str, ntohs(client_addr.sin_port));
        print_log(log_msg, COLOR_INFO);

        struct client_args* args = (struct client_args*)malloc(sizeof(struct client_args));
        if (args == NULL) {
            print_log("Errore allocazione memoria per argomenti client TCP.", COLOR_ERROR);
            closesocket(client_socket);
            continue;
        }
        args->sock = client_socket;
        snprintf(args->adds, sizeof(args->adds), "%02d", tcp_client_id_counter++);
        if (tcp_client_id_counter >= 100) tcp_client_id_counter = 0; // Reset contatore per semplicità

        h_thread = CreateThread(NULL, 0, tcp_client_handler, args, 0, NULL);
        if (h_thread == NULL) {
            snprintf(log_msg, sizeof(log_msg), "Errore creazione thread client TCP (ID %s, Errore WinAPI: %lu).", args->adds, GetLastError());
            print_log(log_msg, COLOR_ERROR);
            free(args);
            closesocket(client_socket);
        } else {
            snprintf(log_msg, sizeof(log_msg), "Thread client TCP (ID %s) avviato per %s:%d.\n", args->adds, client_ip_str, ntohs(client_addr.sin_port));
            print_log(log_msg, COLOR_INFO);
            CloseHandle(h_thread); // Il thread è detached, chiudiamo l'handle subito
        }
    }

    // Pulizia del socket di ascolto e Winsock quando il server non è più 'running'
    closesocket(listen_socket);
    print_log("Socket di ascolto TCP chiuso.", COLOR_INFO);
    WSACleanup();
    print_log("Server TCP terminato e risorse Winsock rilasciate.", COLOR_INFO);
}

void start_serial_server(const char* port_name) {
    HANDLE h_client_listen_serial = INVALID_HANDLE_VALUE;
    char log_msg[256];

    snprintf(log_msg, sizeof(log_msg), "Tentativo di avviare il server di ascolto sulla porta seriale: %s", port_name);
    print_log(log_msg, COLOR_INFO);

    if (!configure_serial_port(port_name, &h_client_listen_serial, SERIAL_BAUD_RATE, SERIAL_PARITY, SERIAL_STOP_BITS, SERIAL_BYTE_SIZE, FALSE)) {
        snprintf(log_msg, sizeof(log_msg), "Impossibile configurare la porta seriale di ascolto %s. Server seriale non avviato.", port_name);
        print_log(log_msg, COLOR_ERROR);
        return;
    }

    snprintf(log_msg, sizeof(log_msg), "Server in ascolto sulla porta seriale %s. Un singolo client puo' connettersi.", port_name);
    print_log(log_msg, COLOR_INFO);

    struct serial_client_args* args = (struct serial_client_args*)malloc(sizeof(struct serial_client_args));
    if (args == NULL) {
        print_log("Errore di allocazione memoria per argomenti thread client seriale.", COLOR_ERROR);
        close_serial_port_handle(&h_client_listen_serial);
        return;
    }
    args->hClientSerial = h_client_listen_serial;
    strncpy(args->adds, "S1", MAX_ADDS -1 ); // Client ID fisso per il client seriale
    args->adds[MAX_ADDS - 1] = '\0'; // Assicura null termination

    HANDLE h_thread = CreateThread(NULL, 0, serial_client_handler, args, 0, NULL);
    if (h_thread == NULL) {
        snprintf(log_msg, sizeof(log_msg), "Errore creazione thread client seriale (Errore WinAPI: %lu).", GetLastError());
        print_log(log_msg, COLOR_ERROR);
        free(args); 
        close_serial_port_handle(&h_client_listen_serial);
        return;
    }

    print_log("Client seriale 'connesso', thread handler avviato. Il server attende la terminazione dell'handler.", COLOR_INFO);
    WaitForSingleObject(h_thread, INFINITE);

    CloseHandle(h_thread);
    close_serial_port_handle(&h_client_listen_serial); // Chiusa qui dopo che il thread l'ha usata.

    snprintf(log_msg, sizeof(log_msg), "Thread client seriale terminato e porta di ascolto %s chiusa.", port_name);
    print_log(log_msg, COLOR_INFO);
}

// === MAIN SERVER ===
// =====================
int main() {
    system("cls"); // Pulisce lo schermo all'avvio
    char choice_buffer[128];
    int choice;

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, BACKGROUND_BLACK | COLOR_TITLE); // Sfondo Nero, Testo Azzurro Brillante
    printf("\n");
    printf("+----------------------------------------------------------+\n");
    printf("|                                                          |\n");
    printf("|                  SERVER TCP STAMPANTE                    |\n");
    printf("|                                                          |\n");
    printf("|                    VERSIONE 3.0.0                        |\n");
    printf("|                                                          |\n");
    printf("+----------------------------------------------------------+\n");
    printf("\n");
    SetConsoleTextAttribute(hConsole, BACKGROUND_BLACK | COLOR_DEFAULT); // Ripristina default: Bianco su Nero

    // === CONFIGURAZIONE MODULO RELÈ (INTERATTIVO) ===
    print_colored("--- Configurazione Modulo Rele ---\n", COLOR_SECTION);
    char relay_choice_buffer[10];
    print_colored("Il modulo rele collegato? (s/n) [s]: ", COLOR_INPUT);
    if (fgets(relay_choice_buffer, sizeof(relay_choice_buffer), stdin) != NULL) {
        relay_choice_buffer[strcspn(relay_choice_buffer, "\r\n")] = 0;
        if (relay_choice_buffer[0] == 'n' || relay_choice_buffer[0] == 'N') {
            g_relay_module_enabled = FALSE;
            print_log("Modulo rele disabilitato dall'utente.", COLOR_WARNING);
        } else { // Default a 's' (sì)
            char com_port_buffer[20];
            print_colored("Inserire la porta COM del rele (default COM9): ", COLOR_INPUT);
            if (fgets(com_port_buffer, sizeof(com_port_buffer), stdin) != NULL) {
                com_port_buffer[strcspn(com_port_buffer, "\r\n")] = 0;
                char final_com_port[20];
                if (strlen(com_port_buffer) == 0) {
                    strcpy(final_com_port, "COM9");
                } else {
                    strcpy(final_com_port, com_port_buffer);
                }

                // La funzione initialize_relay_module ora si chiama relay_init
                relay_init(final_com_port); // Tenta l'inizializzazione

                if (relay_is_ready()) { // Controlla lo stato dopo l'inizializzazione
                    char success_msg[100];
                    snprintf(success_msg, sizeof(success_msg), "Modulo rele inizializzato con successo su %s.", final_com_port);
                    print_log(success_msg, COLOR_SUCCESS);
                    g_relay_module_enabled = TRUE;
                } else {
                    char error_msg[150];
                    snprintf(error_msg, sizeof(error_msg), "ERRORE: Modulo rele non rilevato su %s. Verificare connessione. Il controllo rele sarà disabilitato.", final_com_port);
                    print_log(error_msg, COLOR_ERROR);
                    g_relay_module_enabled = FALSE;
                }
            }
        }
    }
    print_separator();



    // === CONFIGURAZIONE ASCOLTO SERVER (TCP/IP FISSO) ===
    g_server_listen_mode = MODE_TCP_IP; // Server ascolta sempre in TCP/IP
    print_log("Modalita' ascolto server: TCP/IP (fisso).\n", COLOR_INFO);
    print_colored("--- Configurazione Porta Ascolto Server TCP/IP ---\n", COLOR_SECTION);
    print_colored("Inserisci la porta TCP per l'ascolto (default 9999): ", COLOR_INPUT);
    if (fgets(choice_buffer, sizeof(choice_buffer), stdin) != NULL) {
        if (strchr(choice_buffer, '\n') == NULL) { // Se l'input è più lungo del buffer, pulisco
            clear_stdin_buffer();
        }
        if (strlen(choice_buffer) > 1 && choice_buffer[0] != '\n') { // Controlla se l'utente ha inserito qualcosa oltre a INVIO
            int input_port = atoi(choice_buffer);
            if (input_port > 0 && input_port <= 65535) {
                g_server_listen_tcp_port = input_port;
            } else {
                print_log("Porta TCP inserita non valida, uso default 9999.", COLOR_WARNING);
                // g_server_listen_tcp_port resta al suo valore di default (9999), che si presume sia inizializzato globalmente
            }
        }
    }
    char msg_port[50];
    snprintf(msg_port, sizeof(msg_port), "Server ascoltera' sulla porta TCP: %d", g_server_listen_tcp_port);
    print_log(msg_port, COLOR_INFO);
    print_separator();

    // === SCELTA MODALITÀ CONNESSIONE ALLA STAMPANTE FISICA ===
    print_colored("--- Configurazione Connessione Stampante Fisica ---\n", COLOR_SECTION);
    print_colored("Scegli la modalita' di connessione alla stampante fisica:\n", COLOR_INPUT);
    print_colored("1. TCP/IP (Stampante di rete)\n", COLOR_INPUT);
    print_colored("2. Seriale (RS232/UART)\n", COLOR_INPUT);
    print_colored("Inserisci la tua scelta (1 o 2): ", COLOR_INPUT);

    if (fgets(choice_buffer, sizeof(choice_buffer), stdin) != NULL) {
        g_printer_connection_mode = (CommunicationMode)atoi(choice_buffer);
        if (strchr(choice_buffer, '\n') == NULL) { // Se non c'è newline, il buffer è pieno
            clear_stdin_buffer();
        }
    }

    if (g_printer_connection_mode == MODE_TCP_IP) {
        print_log("Connessione stampante: TCP/IP selezionata.\n", COLOR_INFO);
        char ip_prompt[100];
        snprintf(ip_prompt, sizeof(ip_prompt), "Inserisci l'indirizzo IP della stampante (default %s): ", DEFAULT_PRINTER_IP);
        print_colored(ip_prompt, COLOR_INPUT);
        if (fgets(g_printer_conn_ip_address, sizeof(g_printer_conn_ip_address), stdin) != NULL) {
            if (strchr(g_printer_conn_ip_address, '\n') == NULL) { // Se l'input è più lungo del buffer, pulisco
                clear_stdin_buffer();
            }
            g_printer_conn_ip_address[strcspn(g_printer_conn_ip_address, "\r\n")] = 0;
            if (strlen(g_printer_conn_ip_address) == 0) {
                strncpy(g_printer_conn_ip_address, DEFAULT_PRINTER_IP, sizeof(g_printer_conn_ip_address) - 1);
                g_printer_conn_ip_address[sizeof(g_printer_conn_ip_address) - 1] = '\0'; // Ensure null termination
            }
        }

        char port_prompt[100];
        snprintf(port_prompt, sizeof(port_prompt), "Inserisci la porta TCP della stampante (default %d): ", DEFAULT_PRINTER_PORT);
        print_colored(port_prompt, COLOR_INPUT);
        if (fgets(choice_buffer, sizeof(choice_buffer), stdin) != NULL) {
            if (strchr(choice_buffer, '\n') == NULL) { // Se l'input è più lungo del buffer, pulisco
                clear_stdin_buffer();
            }
            if (strlen(choice_buffer) > 1 && choice_buffer[0] != '\n') { // Controlla se l'utente ha inserito qualcosa oltre a INVIO
                g_printer_conn_tcp_port = atoi(choice_buffer);
                if (g_printer_conn_tcp_port <= 0 || g_printer_conn_tcp_port > 65535) {
                    g_printer_conn_tcp_port = DEFAULT_PRINTER_PORT;
                }
            } else {
                g_printer_conn_tcp_port = DEFAULT_PRINTER_PORT;
            }
        }
        char msg_print_tcp[100];
        snprintf(msg_print_tcp, sizeof(msg_print_tcp), "Stampante sara' contattata a %s:%d", g_printer_conn_ip_address, g_printer_conn_tcp_port);
        print_log(msg_print_tcp, COLOR_INFO);
    } else if (g_printer_connection_mode == MODE_SERIAL) {
        print_log("Connessione stampante: Seriale selezionata.\n", COLOR_INFO);
        print_colored("Inserisci il nome della porta COM della stampante (es. COM2): ", COLOR_INPUT);
        if (fgets(g_printer_conn_serial_port_name, sizeof(g_printer_conn_serial_port_name), stdin) != NULL) {
            if (strchr(g_printer_conn_serial_port_name, '\n') == NULL) { // Se l'input è più lungo del buffer, pulisco
                clear_stdin_buffer();
            }
            g_printer_conn_serial_port_name[strcspn(g_printer_conn_serial_port_name, "\r\n")] = 0;
            if (strlen(g_printer_conn_serial_port_name) == 0) {
                print_log("Nome porta COM stampante non valido. Uscita.", COLOR_ERROR);
                return 1;
            }
            // Tentativo di aprire e configurare la porta seriale della stampante subito
            if (!configure_serial_port(g_printer_conn_serial_port_name, &h_printer_comm_port, PRINTER_BAUD_RATE, PRINTER_PARITY, PRINTER_STOP_BITS, PRINTER_BYTE_SIZE, TRUE)) {
                print_log("Impossibile configurare la porta seriale per la stampante. Controllare connessione e nome porta. Uscita.", COLOR_ERROR);
                return 1;
            }
            char msg_print_com[100];
            snprintf(msg_print_com, sizeof(msg_print_com), "Stampante sara' contattata sulla porta COM: %s", g_printer_conn_serial_port_name);
            print_log(msg_print_com, COLOR_INFO);
        } else {
            print_log("Errore lettura nome porta COM stampante. Uscita.", COLOR_ERROR);
            return 1;
        }
    } else {
        print_log("Scelta modalita' connessione stampante non valida. Uscita.", COLOR_ERROR);
        return 1;
    }
    // Avvia il thread del server
    HANDLE h_server_thread = CreateThread(NULL, 0, server_thread_func, (LPVOID)(INT_PTR)g_server_listen_tcp_port, 0, NULL);
    if (h_server_thread == NULL) {
        print_log("Errore nella creazione del thread del server. Uscita.", COLOR_ERROR);
        relay_cleanup();
        return 1;
    }

    print_separator();
    print_log("Server in esecuzione. Digita 'exit' e premi Invio per chiudere.", COLOR_HIGHLIGHT);
    print_separator();

    // Loop per attendere il comando 'exit' dalla console
    char exit_cmd[10];
    while (is_running) {
        if (fgets(exit_cmd, sizeof(exit_cmd), stdin) != NULL) {
            // Rimuove il newline dal comando letto
            exit_cmd[strcspn(exit_cmd, "\r\n")] = 0;

            if (strcmp(exit_cmd, "exit") == 0) {
                is_running = FALSE;
                print_log("Comando di chiusura ricevuto. Arresto del server in corso...\n", COLOR_WARNING);
                if (listen_socket != INVALID_SOCKET) {
                    closesocket(listen_socket);
                    listen_socket = INVALID_SOCKET;
                }
            } else if (strcmp(exit_cmd, "feed") == 0) {
                if (g_relay_module_enabled) {
                    print_log("Comando 'feed' da console: attivo rele per avanzamento carta.", COLOR_INFO);
                    pulse_relay(200);
                } else {
                    print_log("Comando 'feed' non eseguibile: modulo rele non abilitato o non disponibile.", COLOR_ERROR);
                }
            }
        }
    }

    // Attendi la terminazione del thread del server
    WaitForSingleObject(h_server_thread, INFINITE);
    CloseHandle(h_server_thread);

    // Pulizia finale se la stampante era seriale e la porta è aperta
    if (g_printer_connection_mode == MODE_SERIAL && h_printer_comm_port != INVALID_HANDLE_VALUE) {
        close_serial_port_handle(&h_printer_comm_port);
    }

    // Pulizia del modulo relè
    print_log("Pulizia modulo rele...", COLOR_INFO);
    relay_cleanup();

    print_log("Server principale terminato.", COLOR_INFO);
    
    system("cls"); // Pulisce lo schermo prima di uscire
    return 0;
}


// =========================
// === FUNZIONI HELPER SERIALI ===
// =========================
BOOL configure_serial_port(const char* port_name, HANDLE* hSerial, int baud_rate, BYTE parity, BYTE stop_bits, BYTE byte_size, BOOL for_printer_comm) {
    char full_port_name[30];
    // Per porte COM1-COM9, il nome è "COMx". Per COM10 e oltre, è "\\\\.\\COMxx"
    if (strlen(port_name) > 4 && (strncmp(port_name, "COM", 3) == 0 && atoi(port_name + 3) >= 10)) {
        snprintf(full_port_name, sizeof(full_port_name), "\\\\.\\%s", port_name);
    } else {
        strncpy(full_port_name, port_name, sizeof(full_port_name) -1);
        full_port_name[sizeof(full_port_name)-1] = '\0';
    }

    *hSerial = CreateFileA(full_port_name,
        GENERIC_READ | GENERIC_WRITE,
        0,      // must be opened with exclusive-access
        NULL,   // default security attributes
        OPEN_EXISTING, // opens existing device
        for_printer_comm ? 0 : FILE_FLAG_OVERLAPPED, // FILE_FLAG_OVERLAPPED per client seriali se si vuole I/O asincrona, 0 per stampante (sincrona)
        NULL);  // no template file

    if (*hSerial == INVALID_HANDLE_VALUE) {
        char err_msg[100];
        snprintf(err_msg, sizeof(err_msg), "Errore apertura porta %s: %lu", port_name, GetLastError());
        print_log(err_msg, COLOR_ERROR);
        return FALSE;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(*hSerial, &dcbSerialParams)) {
        print_log("Errore GetCommState", COLOR_ERROR);
        CloseHandle(*hSerial); *hSerial = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = byte_size;
    dcbSerialParams.StopBits = stop_bits;
    dcbSerialParams.Parity   = parity;
    dcbSerialParams.fBinary = TRUE;
    dcbSerialParams.fParity = (parity == NOPARITY) ? FALSE : TRUE;
    dcbSerialParams.fOutxCtsFlow = FALSE;
    dcbSerialParams.fOutxDsrFlow = FALSE;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    dcbSerialParams.fOutX = FALSE;
    dcbSerialParams.fInX = FALSE;
    dcbSerialParams.fErrorChar = FALSE;
    dcbSerialParams.fNull = FALSE;
    dcbSerialParams.fAbortOnError = FALSE;

    if (!SetCommState(*hSerial, &dcbSerialParams)) {
        print_log("Errore SetCommState", COLOR_ERROR);
        CloseHandle(*hSerial); *hSerial = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    COMMTIMEOUTS timeouts = {0};
    // Per la stampante (comunicazione sincrona), timeout più brevi potrebbero andare bene.
    // Per i client seriali (se si usa I/O sincrona nel gestore), timeout più lunghi o gestione attenta.
    timeouts.ReadIntervalTimeout         = 50;    // Max time between arrival of two bytes (ms)
    timeouts.ReadTotalTimeoutConstant    = for_printer_comm ? 2000 : 500; // Total timeout for read (ms)
    timeouts.ReadTotalTimeoutMultiplier  = 10;    // Multiplier for read timeout (ms)
    timeouts.WriteTotalTimeoutConstant   = for_printer_comm ? 2000 : 500; // Total timeout for write (ms)
    timeouts.WriteTotalTimeoutMultiplier = 10;    // Multiplier for write timeout (ms)

    if (!SetCommTimeouts(*hSerial, &timeouts)) {
        print_log("Errore SetCommTimeouts", COLOR_ERROR);
        CloseHandle(*hSerial); *hSerial = INVALID_HANDLE_VALUE;
        return FALSE;
    }

    PurgeComm(*hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR); // Pulisce i buffer della porta

    char msg_cfg[150];
    snprintf(msg_cfg, sizeof(msg_cfg), "Porta %s configurata: %d baud, %d data bit, %s parita', %s stop bit.\n", 
        port_name, baud_rate, byte_size, 
        (parity==NOPARITY?"nessuna":(parity==ODDPARITY?"dispari":(parity==EVENPARITY?"pari":"marcata/spazio"))),
        (stop_bits==ONESTOPBIT?"1":(stop_bits==ONE5STOPBITS?"1.5":"2")));
    print_log(msg_cfg, COLOR_SUCCESS);
    return TRUE;
}

void close_serial_port_handle(HANDLE* hComm) {
    if (hComm && *hComm != INVALID_HANDLE_VALUE) {
        CloseHandle(*hComm);
        *hComm = INVALID_HANDLE_VALUE;
        // print_log("Handle porta seriale chiuso.", COLOR_DEBUG); // Log opzionale
    }
}

int read_from_serial_port(HANDLE hComm, char* buffer, int buffer_len) {
    DWORD bytes_read = 0;
    if (!ReadFile(hComm, buffer, buffer_len, &bytes_read, NULL)) { // Sincrono
        DWORD error = GetLastError();
        // ERROR_OPERATION_ABORTED (995) può verificarsi se la porta viene chiusa durante una lettura
        if (error != ERROR_OPERATION_ABORTED) { 
            char err_msg[100];
            snprintf(err_msg, sizeof(err_msg), "Errore ReadFile su seriale: %lu", error);
            print_log(err_msg, COLOR_ERROR);
        }
        return -1; 
    }
    return (int)bytes_read;
}

int write_to_serial_port(HANDLE hComm, const char* data, int data_len) {
    DWORD bytes_written = 0;
    if (!WriteFile(hComm, data, data_len, &bytes_written, NULL)) { // Sincrono
        char err_msg[100];
        snprintf(err_msg, sizeof(err_msg), "Errore WriteFile su seriale: %lu", GetLastError());
        print_log(err_msg, COLOR_ERROR);
        return -1;
    }
    return (int)bytes_written;
}