/*
 * File: client.c
 * Descrizione: Client TCP per comunicazione con server stampante fiscale
 *              Implementa la logica di invio comandi e ricezione risposte
 */

// Disabilita warning per funzioni deprecate di Winsock
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// Definizione dei colori per la console
#define COLOR_DEFAULT 7   // Bianco
#define COLOR_INFO 10     // Verde per info
#define COLOR_ERROR 12    // Rosso per errori
#define COLOR_WARNING 14  // Giallo per warning
#define COLOR_SUCCESS 10  // Verde per successo
#define COLOR_TITLE 11    // Azzurro per titoli
#define COLOR_SECTION 13  // Magenta per sezioni

// Definizione dei colori per le risposte
#define COLOR_RESPONSE 9  // Blu per le risposte
#define COLOR_RESPONSE_ERROR 12 // Rosso per errori nelle risposte
#define COLOR_RESPONSE_SUCCESS 10 // Verde per risposte di successo

// Costanti per la separazione dei comandi
#define SEPARATOR "------------------------------------------------------------"
#define SEPARATOR_COLOR 8 // Grigio scuro

// Inclusione delle librerie necessarie
#include <stdio.h>      // I/O standard
#include <string.h>     // Funzioni stringhe
#include <winsock2.h>   // Socket Windows
#include <windows.h>    // Funzioni Windows
#include <stdlib.h>     // Funzioni standard

// Linka automaticamente la libreria ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

// Prototipi delle funzioni
void mostra_comandi();
void print_separator();
void print_colored(const char* msg, int color);
void mostra_stato(const char* comando, const char* risposta, int successo);

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

// Mostra lo stato di un'operazione
void mostra_stato(const char* comando, const char* risposta, int successo) {
    printf("%s: %s - %s\n", comando, risposta, successo ? "SUCCESSO" : "ERRORE");
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
 
    printf("+----------------------------------------------------------+\n");
    printf("|                CLIENT TCP - CONSOLE v1.3.1               |\n");
    printf("+----------------------------------------------------------+\n");
    printf("\nInformazioni di sistema:\n");
    printf("- Versione client: 1.3.1\n");
    // --- Configurazione dinamica IP e porta ---
    char ip_server[64] = "10.0.70.13";
    char porta_str[16] = "9999";
    int porta = 9999;
    printf("- Server di default: %s:%d\n", ip_server, porta);
    printf("- Protocollo: TCP\n");
    print_separator();
    // Richiedi IP
    printf("Inserisci IP server [default: %s]: ", ip_server);
    fgets(ip_server, sizeof(ip_server), stdin);
    ip_server[strcspn(ip_server, "\n")] = 0;
    if (strlen(ip_server) == 0) strcpy(ip_server, "10.0.70.13");
    // Richiedi porta
    printf("Inserisci porta server [default: %d]: ", porta);
    fgets(porta_str, sizeof(porta_str), stdin);
    porta_str[strcspn(porta_str, "\n")] = 0;
    if (strlen(porta_str) > 0) porta = atoi(porta_str);
    // Stampa riepilogo connessione
    printf("[INFO] Connessione a %s:%d\n", ip_server, porta);
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
 
    // Tenta la connessione al server
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        set_color(12); // Rosso
        printf("[X] Errore connessione.\n");
        set_color(7);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

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
        fgets(message, sizeof(message), stdin); // Legge una riga da tastiera
 
        // Rimuove il carattere di newline finale
        message[strcspn(message, "\n")] = 0;
 
        // Se l'utente digita "esc", esce dal ciclo
        if (strcmp(message, "esc") == 0) {
            break;
        }
        
        // Mostra aiuto
        if (strcmp(message, "help") == 0) {
            mostra_comandi();
            continue;
        }
 
        // Modalità invio multiplo di messaggi
        if (strcmp(message, "multi") == 0) {
            char batch[10][1024]; // Array per massimo 10 messaggi
            int batch_count = 0;
            printf("Inserisci i messaggi da inviare (riga vuota per terminare):\n");
            // Ciclo per inserire più messaggi
            while (batch_count < 10) {
                printf("> ");
                fgets(batch[batch_count], sizeof(batch[batch_count]), stdin);
                batch[batch_count][strcspn(batch[batch_count], "\n")] = 0;
                if (strlen(batch[batch_count]) == 0) break; // Riga vuota per terminare
                batch_count++;
            }
            // Invia i messaggi uno alla volta
            int batch_errors = 0;
            for (int i = 0; i < batch_count; i++) {
                char to_send[1024];
                snprintf(to_send, sizeof(to_send), "%s\n", batch[i]); // Aggiunge newline
                if (send(sock, to_send, strlen(to_send), 0) < 0) { // Invia il messaggio
                    set_color(12);
                    printf("[X] Errore invio messaggio batch.\n");
                    set_color(7);
                    batch_errors++;
                    break;
                }
                memset(server_reply, 0, sizeof(server_reply));
                int recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
                if (recv_size <= 0) {
                    set_color(12);
                    printf("[X] Batch %d: Errore o connessione chiusa dal server.\n", i+1);
                    set_color(7);
                    batch_errors++;
                    break;
                }
                if (recv_size == 1 && (server_reply[0] == 0x06 || server_reply[0] == 0x15)) {
                    if (server_reply[0] == 0x06) {
                        printf("Batch %d: Ricevuto ACK dal server. Attendo risposta...\n", i+1);
                        memset(server_reply, 0, sizeof(server_reply));
                        recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
                        if (recv_size <= 0) {
                            set_color(12);
                            printf("[X] Batch %d: Errore o connessione chiusa dal server.\n", i+1);
                            set_color(7);
                            batch_errors++;
                            break;
                        }
                    } else {
                        set_color(12);
                        printf("[X] Batch %d: Ricevuto NAK dal server. Riprova.\n", i+1);
                        set_color(7);
                        batch_errors++;
                        continue;
                    }
                } else if (recv_size > 0 && (unsigned char)server_reply[0] == 0x02) {
                    // Ricevuto direttamente il pacchetto protocollo (STX)
                    // Niente da fare, il buffer è già pronto
                } else {
                    // Risposta inattesa: ignora silenziosamente oppure conta gli errori
                    batch_errors++;
                    continue;
                }
                // Stampa la risposta in modo più leggibile
                printf("Batch %d: Risposta dal server:\n", i+1);
                set_color(10);
                printf("[OK] %s\n", server_reply);
                set_color(7);
            }
            if (batch_errors > 0) {
                continue; // Torna all'inizio del ciclo principale
            }
        } else {
            // Invio singolo messaggio
            char to_send[1024];
            snprintf(to_send, sizeof(to_send), "%s\n", message); // Aggiunge newline
            if (send(sock, to_send, strlen(to_send), 0) < 0) {
                set_color(12);
                printf("[X] Errore invio messaggio.\n");
                set_color(7);
                continue;
            }
            // Mostra conferma invio comando
            mostra_conferma_comando(message);
            memset(server_reply, 0, sizeof(server_reply));
            int recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
            if (recv_size <= 0) {
                set_color(12);
                printf("[X] Errore o connessione chiusa dal server.\n");
                set_color(7);
                break;
            }
            if (recv_size == 1 && (server_reply[0] == 0x06 || server_reply[0] == 0x15)) {
                if (server_reply[0] == 0x06) {
                    printf("Ricevuto ACK dal server. Attendo risposta...\n");
                    memset(server_reply, 0, sizeof(server_reply));
                    recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
                    if (recv_size <= 0) {
                        set_color(12);
                        printf("[X] Errore o connessione chiusa dal server.\n");
                        set_color(7);
                        break;
                    }
                } else {
                    set_color(12);
                    printf("[X] Ricevuto NAK dal server. Riprova.\n");
                    set_color(7);
                    continue;
                }
            } else if (recv_size > 0 && (unsigned char)server_reply[0] == 0x02) {
                // Ricevuto direttamente il pacchetto protocollo (STX)
                // Niente da fare, il buffer è già pronto
            } else {
                // Risposta inattesa: ignora silenziosamente oppure conta gli errori
                continue;
            }
            // Stampa la risposta in modo più leggibile
            set_color(10);
            printf("[OK] %s\n", server_reply);
            set_color(7);
        }
    // Riceve la risposta dal server (può essere ACK/NAK o direttamente il pacchetto protocollo)
    int recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
    int successo = 0;
        
    if (recv_size == 1 && (server_reply[0] == 0x06 || server_reply[0] == 0x15)) {
        if (server_reply[0] == 0x06) {
            printf("Ricevuto ACK dal server. Attendo risposta...\n");
            // Ricevi la risposta protocollo
            recv_size = recv(sock, server_reply, sizeof(server_reply) - 1, 0);
            successo = 1;
        } else {
            printf("Ricevuto NAK dal server. Riprova.\n");
            continue;
        }
    } else if (recv_size > 0 && (unsigned char)server_reply[0] == 0x02) {
        // Ricevuto direttamente il pacchetto protocollo (STX)
        successo = 1;
    } else {
        // Risposta inattesa: ignora silenziosamente oppure conta gli errori
        continue;
    }
        
        // Mostra lo stato dell'operazione
        mostra_stato(message, server_reply, successo);

        if (recv_size > 0) {
            print_separator();
            set_color(9); // Blu
            printf("[RISP] Risposta dal server (dec): ");
            set_color(7);
            for (int i = 0; i < recv_size; i++) {
                printf("%d ", (unsigned char)server_reply[i]);
            }
            printf("\n");

            set_color(9);
            printf("[RISP] Risposta dal server (hex): ");
            set_color(7);
            for (int i = 0; i < recv_size; i++) {
                printf("%02X ", (unsigned char)server_reply[i]);
            }
            printf("\n");

            // Cerca i delimitatori STX (0x02) e ETX (0x03) per estrarre il payload
            int stx = -1, etx = -1;
            for (int i = 0; i < recv_size; i++) {
                if ((unsigned char)server_reply[i] == 0x02 && stx == -1) stx = i;
                if ((unsigned char)server_reply[i] == 0x03) { etx = i; break; }
            }
            if (stx != -1 && etx != -1 && etx > stx) {
                int header_len = 6;      // Numero di byte da saltare dopo STX
                int footer_len = 3;      // Numero di byte da escludere prima di ETX
                int payload_offset = stx + 1 + header_len;
                int payload_len = etx - footer_len - payload_offset;
                if (payload_offset >= 0 && payload_len > 0 && payload_offset + payload_len <= etx) {
                    printf("Campo dati: ");
                    fwrite(server_reply + payload_offset, 1, payload_len, stdout); // Stampa payload
                    printf("\n");
                }
                // Invia ACK al server per confermare la ricezione
                unsigned char ack = 0x06;
                send(sock, (const char*)&ack, 1, 0);
                printf("ACK inviato al server.\n");
            } else {
                printf("Messaggio non valido o delimitatori mancanti.\n");
                // Invia NAK se il messaggio non è valido
                unsigned char nak = 0x15;
                send(sock, (const char*)&nak, 1, 0);
                printf("NAK inviato al server.\n");
            }
        } else {
            printf("Errore o connessione chiusa dal server.\n");
            break;
        }
    }   
    // Chiude il socket e libera le risorse Winsock
    closesocket(sock);
    WSACleanup();
 
    return 0;
}
 