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
   ```
3. Compila il client:
   ```sh
   cl client.c /link ws2_32.lib
   ```

### Con MinGW (GCC)
1. Apri il prompt dei comandi.
2. Compila il server:
   ```sh
   gcc server.c -o server.exe -lws2_32
   ```
3. Compila il client:
   ```sh
   gcc client.c -o client.exe -lws2_32
   ```

## Esecuzione
1. Avvia prima il server:
   ```sh
   server.exe
   ```
2. Avvia il client in un altro terminale:
   ```sh
   client.exe
   ```

## Configurazione
- **Modalità TCP/IP**: Inserire l’IP e la porta del server quando richiesto (default: IP `10.0.70.21`, porta `3000` per il server, IP `10.0.70.20`, porta `9999` per il client).
- **Modalità Seriale**: Inserire la porta COM e i parametri richiesti.

Le impostazioni possono essere modificate all’avvio tramite input utente. È possibile estendere il progetto per caricare la configurazione da file.

## Funzionalità principali
- Comunicazione con la stampante tramite TCP o seriale
- Gestione multi-thread lato server (in sviluppo per TCP)
- Gestione centralizzata degli errori tramite `error_table.h`
- Interfaccia utente a colori e messaggi informativi

## Note aggiuntive
- Il server TCP attualmente accetta una sola connessione alla volta (la gestione multi-client è in sviluppo).
- Per la modalità seriale, assicurarsi che la porta selezionata sia disponibile e non usata da altri programmi.

## Autori
- Luca Pillon
- Elsi Sualj

---
Per domande o suggerimenti, aprire una issue o contattare l’autore.
