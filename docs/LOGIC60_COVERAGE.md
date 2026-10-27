# Copertura della logica a 60 Hz

Il ciclo principale chiama `func_00028FD8` una volta per immagine. Il wrapper
in `ports/dbz3/dbz3_loop_prof.cpp` accumula tempo reale e ripete i task
classificati come sola logica. I task che emettono comandi GCM vengono
classificati come disegno; solo `0x001DBC58` (Dragon Universe) è oggi
ripetuto con il suo tratto di rendering saltato. Le altre chiamate del ciclo
principale e i task di disegno non ricevono automaticamente un secondo passo.

I log `run22/t26-loop-prof.log` confermano che il numero *globale* di passi
logici non basta per valutare la velocità di ogni modalità. Una finestra DU
ha 23 immagini in circa cinque secondi, 18,2 passi logici/s e il limite
attuale di quattro passi per immagine. Altre finestre DU arrivano a 40,8 e
55,0 passi/s. A 30 FPS il limite di quattro consente teoricamente due passi
per immagine; i casi ancora legati ai frame richiedono identificare quale
task aggiorna l'animazione e se usa già un delta temporale reale.

La build diagnostica `logic-coverage` aggiunge a ogni riga `task fn=...` del
log `t26-loop-prof.log` i campi `extra/pass`, `extra_avg_us` e `class`.
`extra/pass=0` per un task che aggiorna l'animazione in un percorso a 30 FPS
lo rende un candidato da esaminare; non prova da solo che la sua animazione
sia lenta, perché può usare un delta temporale reale. I task che fanno solo
disegno non vanno ripetuti indiscriminatamente: possono allocare risorse o
avviare lavoro asincrono, e hanno già causato corruzione in prove precedenti.

Per la prossima prova servono sessioni separate di duello, mappa DU, filmati,
menu e schermata livello, annotando quale appare lenta. Con i nuovi campi
si possono confrontare il task attivo e il rapporto fra passi extra e passaggi
grafici, poi dividere aggiornamento e disegno nei soli task interessati.
