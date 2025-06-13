# Server/Client per Stampante Fiscale

## Descrizione
Questo progetto implementa un sistema client/server per la comunicazione con una stampante fiscale. Il server è progettato per essere robusto, gestire client multipli simultaneamente e controllare l'alimentazione della stampante tramite un relè USB. L'applicazione è scritta in C e ottimizzata per l'ambiente Windows.

## Struttura del Progetto
- `server.c`: Il cuore del server, gestisce le connessioni, i thread e la logica principale.
- `client.c`: Un client di test per inviare comandi al server.
- `relay_control.c` / `.h`: Modulo per il controllo del relè USB (modello SH-UR01A).
- `error_table.h`: Definizione e gestione centralizzata dei codici di errore.
- `build/`: Contiene gli eseguibili compilati (`server.exe`, `client.exe`).
- `README.md`: Questo file.

## Requisiti
- **Sistema operativo**: Windows
- **Compilatore**: MinGW/GCC
- **Dipendenze**: Libreria Winsock2 (`ws2_32`)

## Compilazione
Per compilare il progetto, apri un terminale (Prompt dei comandi o PowerShell) nella cartella principale.

1.  **Compila il Server:**
    ```sh
    gcc server.c relay_control.c -o build/server.exe -lws2_32
    ```

2.  **Compila il Client:**
    ```sh
    gcc client.c -o build/client.exe -lws2_32
    ```

## Esecuzione
1.  **Avvia il server** da un terminale:
    ```sh
    .\build\server.exe
    ```
    Il server ti guiderà nella configurazione iniziale.

2.  **Avvia uno o più client** da altri terminali:
    ```sh
    .\build\client.exe
    ```

## Configurazione di Default
Il server e il client sono pre-configurati con i seguenti valori di default per semplificare l'avvio:
- **Server IP (per connessione client)**: `10.0.70.11` (localhost)
- **Server Port (in ascolto)**: `9999`
- **Stampante IP (se in modalità TCP/IP)**: `10.0.70.32`
- **Stampante Port (se in modalità TCP/IP)**: `3000`

Questi valori possono essere modificati all'avvio del server, se necessario.

## Funzionalità Principali
-   **Architettura Multi-Thread**: Il server utilizza un thread dedicato per ogni client TCP, garantendo la gestione di connessioni multiple e simultanee senza bloccare l'operatività principale.
-   **Doppia Modalità di Connessione**: Il server può comunicare con la stampante fisica tramite **TCP/IP** (rete) o **porta Seriale** (RS232/UART), offrendo flessibilità a seconda dell'hardware disponibile.
-   **Controllo Relè USB**: Integra il controllo di un relè USB (modello SH-UR01A) per accendere e spegnere fisicamente la stampante, simulando un controllo di alimentazione completo.
-   **Chiusura Controllata (Graceful Shutdown)**: Implementa un meccanismo di chiusura sicuro tramite il comando `exit`. Questo garantisce la terminazione pulita di tutti i thread, la chiusura delle connessioni e lo spegnimento del relè.
-   **Interfaccia Utente a Colori**: La console utilizza output colorato per migliorare la leggibilità di log, errori e messaggi di stato, rendendo il monitoraggio più intuitivo.
-   **Configurazione Dinamica all'Avvio**: Permette di personalizzare le porte e gli indirizzi IP a ogni avvio, utilizzando valori di default intelligenti per accelerare i test.

## Autori
- Luca Pillon
- Elsi Sualj

---
*Progetto sviluppato con l'assistenza dell'AI agent Cascade.*
