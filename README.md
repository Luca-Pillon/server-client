# Server/Client Stampante Fiscale

## Descrizione
Questo progetto implementa un sistema client/server per la comunicazione con una stampante fiscale, tramite TCP/IP o porta seriale. Il client permette di inviare comandi e ricevere risposte dalla stampante, mentre il server gestisce la connessione e la comunicazione con il dispositivo.

## Struttura del progetto
- `server.c`: Server che gestisce la comunicazione con la stampante (TCP o seriale)
- `client.c`: Client che invia comandi e riceve risposte
- `error_table.h`: Definizione e gestione centralizzata dei codici di errore

## Requisiti
- **Sistema operativo**: Windows
- **Compilatore**: Visual Studio, MinGW o simili
- **Dipendenze**: Winsock2 (`ws2_32.lib`)

## Compilazione
### Con Visual Studio
1. Apri il prompt dei comandi di Visual Studio.
2. Compila il server:
   ```sh
   cl server.c /link ws2_32.lib

Per compilare il progetto, sono necessari un compilatore C (come GCC/MinGW) e le librerie Winsock.

1.  Apri un terminale (Prompt dei comandi o PowerShell).
2.  Naviga nella cartella del progetto.
3.  **Compila il server:**
    Il server ora include il controllo del relè, quindi è necessario compilare entrambi i file sorgente.
    ```sh
    gcc server.c relay_control.c -o server.exe -lws2_32
    ```
4.  **Compila il client:**
    ```sh
    gcc client.c -o client.exe -lws2_32
    ```

## Esecuzione

1.  **Avvia il server** in un terminale:
    ```sh
    server.exe
    ```
    Il server si configurerà tramite una serie di domande e poi si metterà in ascolto.

2.  **Per chiudere il server in modo sicuro**, digita `exit` e premi Invio nello stesso terminale del server. Questo garantirà che il relè venga spento correttamente.

3.  **Avvia uno o più client** in altri terminali:
    ```sh
    client.exe
    ```

## Configurazione all'avvio

Il server richiede le seguenti informazioni all'avvio:

-   **Porta di ascolto del server** (default: `9999`).
-   **Modalità di connessione alla stampante fisica** (TCP/IP o Seriale).
-   **Dettagli di connessione della stampante** (IP/Porta o Porta COM).
-   **Porta COM del relè** (attualmente fissa nel codice come `COM9` in `server.c`).

## Funzionalità principali

-   **Architettura Multi-Thread:** Il server è completamente multi-thread e gestisce ogni client TCP in un processo separato, permettendo connessioni multiple e simultanee.
-   **Controllo Relè USB:** Integra il controllo di un relè USB (modello SH-UR01A) per accendere e spegnere fisicamente la stampante all'avvio e alla chiusura del server.
-   **Chiusura Controllata (Graceful Shutdown):** Implementa un meccanismo di chiusura sicuro tramite il comando `exit`, che garantisce la terminazione pulita dei thread, la chiusura dei socket e lo spegnimento del relè.
-   **Doppia Modalità di Connessione:** Comunica con la stampante fisica sia tramite TCP/IP (rete) che tramite porta Seriale (RS232/UART).
-   **Interfaccia Utente a Colori:** Utilizza una console con output colorato per migliorare la leggibilità di log, errori e messaggi di stato.
-   **Gestione Errori Centralizzata:** Usa `error_table.h` per fornire risposte di errore standardizzate e coerenti.
- Per la modalità seriale, assicurarsi che la porta selezionata sia disponibile e non usata da altri programmi.

## Autori
- Luca Pillon
- Elsi Sualj

---
Per domande o suggerimenti, aprire una issue o contattare l’autore.
