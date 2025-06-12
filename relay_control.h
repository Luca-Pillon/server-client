#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <windows.h>

// Inizializza il modulo relè sulla porta specificata.
void relay_init(const char* port);

// Invia il comando per accendere il relè.
void relay_on(void);

// Invia il comando per spegnere il relè.
void relay_off(void);

// Rilascia le risorse e chiude la porta del relè.
void relay_cleanup(void);

// Controlla se il relè è stato inizializzato correttamente.
// Ritorna 1 se pronto, 0 altrimenti.
int relay_is_ready(void);

/**
 * @brief Simula la pressione di un pulsante attivando il relè per una breve durata.
 * 
 * @param duration_ms La durata in millisecondi per cui il relè deve rimanere acceso.
 */
void pulse_relay(int duration_ms);

#endif // RELAY_CONTROL_H
