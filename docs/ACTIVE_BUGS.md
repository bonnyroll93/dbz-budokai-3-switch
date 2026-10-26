# Bug attivi

Indagine foto e log `run07`: pelle bianca e distorsione sull'acqua ancora aperte; importati altri due vertex shader da catture precedenti, senza attribuire loro una risoluzione visiva non verificata. La mappa DU ha un costo elevato nelle passate extra che ripetono aggiornamento e disegno. Dettagli in [DRAGON_UNIVERSE_RUN07.md](DRAGON_UNIVERSE_RUN07.md).

Aggiornamento `run05`: l'ottimizzazione dell'ordinamento porta la schermata esperienza/livello a circa 14 FPS, ancora sotto l'obiettivo di 30. Il task di disegno principale impiega 58–65 ms per passaggio.

- **Input — risolto nella prova utente, da confermare nelle altre scene:** disabilitare `g_pad_freeze` ha risolto i comandi persi o ritardati. La lettura nelle passate extra è ora permanente. Log della prova: `run06`; nessuna conferma specifica sul blocco password.
- **Modalità Tournament:** non funziona correttamente e il gioco può bloccarsi.
- **Shader e alpha:** problemi di alpha con il pubblico nell'arena Tournament e con altre texture.
- **Dragon Universe:** la skin del personaggio appare bianca.
- **Shader dell'acqua:** il personaggio si deforma mentre si trova nell'acqua.
- **Dragon Universe, dopo il combattimento:** la schermata esperienza e livello scende a circa 7–8 FPS nella sessione `run03`.
- **Generazione password:** a volte la schermata resta bloccata, la password non è visibile e nessun input permette di proseguire. Da riprodurre con una raccolta di log dedicata; il log `run03` non identifica questa schermata.
