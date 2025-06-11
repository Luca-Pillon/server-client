# Server/Client Stampante Fiscale

## Descrizione
Questo progetto implementa un sistema client/server per la comunicazione con una stampante fiscale. Il server è progettato per essere robusto, gestire client multipli simultaneamente e controllare l'alimentazione della stampante tramite un relè USB.

## Struttura del progetto
- `server.c`: Il cuore del server, gestisce le connessioni, i thread e la logica principale.
- `client.c`: Un client di test per inviare comandi al server.
- `relay_control.c` / `relay_control.h`: Modulo per il controllo del relè USB SH-UR01A.
- `error_table.h`: Definizione e gestione centralizzata dei codici di errore.
- `README.md`: Questo file.

## Requisiti
- **Sistema operativo**: Windows
- **Compilatore**: MinGW/GCC
- **Dipendenze**: Libreria Winsock2 (`ws2_32`)

## Compilazione

Per compilare il progetto, apri un terminale (Prompt dei comandi o PowerShell) nella cartella del progetto.

1.  **Compila il Server:**
    Il comando compila insieme il server e il modulo di controllo del relè.
    ```sh
    gcc server.c relay_control.c -o server.exe -lws2_32
    ```

2.  **Compila il Client:**
    ```sh
    gcc client.c -o client.exe -lws2_32
    ```

## Esecuzione

1.  **Avvia il server** in un terminale:
    ```sh
    .\server.exe
    ```
    Il server ti guiderà nella configurazione iniziale (porte, IP, etc.) e poi si metterà in ascolto.

2.  **Chiudi il server in modo sicuro**:
    Per terminare il server, scrivi `exit` e premi Invio nella sua finestra. Questo avvierà la procedura di chiusura controllata, spegnendo anche il relè.

3.  **Avvia uno o più client** in altrettanti terminali:
    ```sh
    .\client.exe
    ```

## Funzionalità Principali

-   **Architettura Multi-Thread:** Il server gestisce ogni client TCP in un thread dedicato, permettendo connessioni multiple e simultanee.
-   **Controllo Relè USB:** Integra il controllo di un relè USB (modello SH-UR01A) per simulare l'accensione e lo spegnimento della stampante.
-   **Chiusura Controllata (Graceful Shutdown):** Implementa un meccanismo di chiusura sicuro tramite il comando `exit`, che garantisce la terminazione pulita dei processi e lo spegnimento del relè.
-   **Doppia Modalità di Connessione:** Il server può comunicare con la stampante fisica sia tramite TCP/IP (rete) che tramite porta Seriale (RS232/UART).
-   **Interfaccia Utente a Colori:** Utilizza una console con output colorato per migliorare la leggibilità di log, errori e messaggi di stato.
-   **Configurazione all'Avvio:** Permette di configurare le porte e gli indirizzi IP a ogni avvio, con valori di default per semplicità.

## Autori
- Luca Pillon
- Elsi Sualj

---
*Progetto sviluppato con l'assistenza dell'AI agent Cascade.*
