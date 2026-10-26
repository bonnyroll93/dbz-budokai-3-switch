# Roadmap: 30 FPS stabili

## Obiettivo

Raggiungere e mantenere 30 FPS stabili nelle seguenti situazioni:

- Duello.
- Mappa di Dragon Universe.
- Schermata mostrata dopo un combattimento in Dragon Universe, quando il personaggio sale di livello.

## Criterio di completamento

L'obiettivo è raggiunto quando il gioco mantiene 30 FPS nelle tre situazioni indicate durante sessioni ripetute, senza cali persistenti o blocchi dell'immagine.

## Esito dell'ultima build e ritorno alla base (26 ottobre 2026)

I log della Switch sono stati raccolti in `build/logs/dbz3/dev/run10`. La build `fps-three-scenes` attivava insieme tre modifiche: record da 64 bit nell'ordinamento della schermata livello, trasformazione nativa `func_0001F1E8` nella mappa e tabella per cercare le pipeline grafiche. Dopo il calo percepito, tutte e tre sono state annullate; `run10` resta conservato per il confronto.

Il ritmo medio di presentazione dell'intera sessione è 36,1/s in `run10` contro 40,1/s in `run09`, ma le sessioni coprono scene e durate diverse: questi valori non isolano la causa del calo. Nelle finestre della mappa, il task `0x001DBC58` richiede circa 17–20 ms per passata regolare in `run10` e 17–23 ms in `run09`; in entrambi i casi le finestre sostenute restano intorno a 23–30 passaggi/s. La schermata livello resta il problema maggiore: nell'ultima finestra di `run10`, il task `0x001D1604` richiede 56,6 ms per passaggio e il ciclo ne completa 67 in 5,02 s. `t23-draw-prof.log` registra 70.943 draw in 5,019 s, circa 834 per frame registrato. Nei picchi finali di `t29-spikes.log` non risultano attese GPU, upload, readback o nuove pipeline.

La build di riferimento dopo il revert è stata compilata e pacchettizzata come `build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp` (`road30-revert-baseline`), senza campionatore PPU. È stata provata sulla Switch in `run11`. Riprendere con una sola modifica per build e misurare separatamente duello, mappa e schermata livello nelle stesse condizioni. Per la schermata livello, il profilo `run10/t23-ppu-hot.log` attribuisce 571 campioni su 3893 del task a `func_00030790`: esaminare le sue chiamate e i draw prodotti prima di ridurre lavoro ripetuto. La funzione è una pista, non una causa dimostrata del tempo totale.

### Prova della base ripristinata: `run11`

L'utente riferisce che la build ripristinata funziona decisamente meglio. I nuovi log sono in `build/logs/dbz3/dev/run11`. Il file `t23-ppu-hot.log` è identico byte per byte a quello di `run10`: è rimasto sulla SD dalla build precedente e non è un profilo di `run11`.

La media dell'intera sessione è 37,1 presentazioni/s contro 36,1 in `run10`; le durate e le scene non coincidono, quindi la differenza aggregata non misura il guadagno della build. Nelle nove finestre sostenute della mappa DU, il ciclo completa circa 22,7–29,7 passaggi/s e il task `0x001DBC58` richiede circa 17,1–21,5 ms per passata regolare. La schermata livello parte ancora con 30 presentazioni in 1,8–2,2 s e il task `0x001D1604` arriva a 58,4 ms per passaggio; le finestre successive risalgono, ma non dimostrano 30 FPS stabili per tutta la schermata. Durante l'ingresso nella schermata, `t23-draw-prof.log` registra 72.273–78.162 draw in circa cinque secondi, circa 900 per frame registrato.

Mantenere questa build come riferimento. Il prossimo intervento deve misurare quali draw della schermata livello sono ripetuti e quanto costa `func_00030790`, poi modificare un solo percorso e confrontarlo con `run11` nella stessa schermata. Non attribuire al file PPU rimasto sulla SD alcuna misura della build ripristinata.

## Diagnosi: schermata esperienza e livello

Per la mappa, la raccolta `run07` individua un problema distinto: il task misto logica/disegno `0x001DBC58` viene ripetuto nei passi extra e poi i suoi comandi grafici sono scartati. Il lavoro CPU rimane: circa 17–21 ms nella passata regolare, più 15–43 ms complessivi di passi extra per immagine nelle finestre analizzate. Priorità: separare aggiornamento e disegno mantenendo il tempo reale. Misure, shader importati e verifiche successive in [DRAGON_UNIVERSE_RUN07.md](DRAGON_UNIVERSE_RUN07.md).

La sessione Switch `build/logs/dbz3/dev/run03` identifica il calo dopo un combattimento in Dragon Universe, quando appare la schermata esperienza e livello. Prima della transizione, 30 frame richiedono circa 0,9–1,0 secondi; durante la schermata ne richiedono circa 3,7–4,1 secondi (7–8 FPS). Il profilo `t26-loop-prof.log` attribuisce circa 138–156 ms per passaggio al task guest `0x001D1604`, che richiama `func_0005AD60`. `t23-draw-prof.log` registra circa 30.000 draw call in cinque secondi, pari a circa 750–800 per frame. Gli ultimi picchi di `t29-spikes.log` non mostrano upload di texture, nuove pipeline o attese GPU: il costo principale osservato è nella preparazione CPU dei comandi grafici.

## Prossimi interventi

### Esito della build `level-sort-scalar` (`run05`)

La scansione scalare riduce il tempo del task `0x001D1604` da circa 138–156 ms a 58–65 ms per passaggio nella schermata livello. Il campionatore PPU passa da 2873 campioni in `func_0005AD60` su 4450 campioni del task (`run04`) a 237 su 2263 (`run05`, finestra mista). Il ciclo principale completa circa 69–72 passaggi in cinque secondi, cioè circa 14 al secondo: il traguardo di 30 FPS non è ancora raggiunto. Nei picchi osservati non ci sono attese GPU. La prossima indagine riguarda le chiamate più costose rimaste nel task, a partire da `func_00030790`, e la quantità di lavoro di disegno ripetuto. Il valore `gcm_kb/pass` di circa 300 indica KiB di comandi, non draw call; per queste usare `t23-draw-prof.log`.

Il profilo aggiuntivo `build/logs/dbz3/dev/run04/t23-ppu-hot.log` localizza il costo nel corpo di `func_0005AD60`: nell'ultima finestra di cinque secondi, 2873 dei 4450 campioni del task `0x001D1604` sono in questa funzione. Il suo ciclo di ordinamento della lista di disegno confronta una sola chiave per elemento, ma esegue caricamenti e conversioni vettoriali a ogni confronto. Una versione diagnostica sostituisce soltanto la scansione interna con letture scalari, lasciando gli scambi e le chiamate di disegno originali. Serve una nuova misura sulla Switch prima di attribuirle un guadagno di FPS.

1. Profilare le chiamate interne a `func_0005AD60` e il numero di draw della schermata, distinguendo elementi che cambiano da quelli statici. Verificare anche se il numero di draw aumenta con i passaggi logici multipli della scena.
2. Ridurre la preparazione ripetuta dei comandi grafici solo dopo aver identificato quali dati sono invariati. Confrontare l'immagine e il comportamento della schermata prima e dopo ogni modifica.
3. Misurare nuovamente la scena su Switch: tempo per frame, draw per frame, tempo del task `0x001D1604` e risposta ai comandi. La prima soglia è scendere sotto 33,3 ms per frame in modo continuativo.
4. Eseguire raccolte separate per duello e mappa Dragon Universe, così da stabilire il loro collo di bottiglia senza confonderlo con la schermata di livello.

### Raccolta del profilo delle funzioni PPU

La build diagnostica si crea con `scripts/build/switch-dev.ps1 -Game dbz3 -Feature level-profile -ProfilePpuFunctions -Jobs 2`. Avvia automaticamente il campionatore PPU e scrive `t23-ppu-hot.log` sulla SD, senza richiedere `diag.on`. Riprodurre la schermata esperienza/livello per almeno 20 secondi e poi raccogliere i log con `scripts/pull-switch-logs.ps1 -Game dbz3`. Confrontare le funzioni più frequenti nel task `0x001D1604` con `t26-loop-prof.log` prima di cambiare il renderer. La build normale si ottiene omettendo `-ProfilePpuFunctions`.

## Blocco della schermata password

La schermata di generazione password può restare senza password visibile e non rispondere ai comandi. Una sessione dedicata deve registrare il percorso dal combattimento alla schermata bloccata e alcuni tentativi di input, per verificare se i comandi arrivano al gioco e se il rendering continua. Risolvere questo blocco è necessario per completare il percorso di Dragon Universe e misurare le schermate successive.

## Responsività degli input

La prova con `pad-repoll` ha risolto il problema degli input secondo il riscontro dell'utente. La lettura del pad nelle passate logiche extra è ora permanente in `dbz3_loop_prof.cpp`: non imposta più `g_pad_freeze` e non richiede il file sulla SD. Il regolatore della logica a 60 Hz e il ritmo di rendering restano gestiti dal codice di temporizzazione esistente.

I log della prova sono in `build/logs/dbz3/dev/run06`. Nel tratto finale il ciclo principale completa 149 passaggi in 5,019 secondi (29,7/s) e la logica riporta 60,0 passi/s. Su 390 cambi dei pulsanti abbinati, dal campionamento registrato alla consegna guest passano 3 ms in mediana, 9 ms al 95° percentile, con un massimo di 133 ms; non è la latenza dalla pressione fisica. Non risultano nuovi file di crash rispetto a `run05`. Non compare il task `0x001D1604` della schermata esperienza: questa raccolta non aggiorna la sua misura di FPS né dimostra la risoluzione del blocco password. La build `pad-live-permanent` è stata compilata e pacchettizzata con successo; va installata per usare il comportamento senza marker SD.

Correzione della diagnosi precedente: `t21-input.log` registra cambi di stato, e `guest_read` viene scritto soltanto quando cambiano i pulsanti. La mediana di 160 ms e il percentile di 469 ms calcolati su `run04` erano intervalli tra cambi registrati, non intervalli tra tutte le chiamate a `cellPadGetData`. Non permettono di misurare la frequenza di polling o contare le pressioni fisiche perse.

La modifica `input-queue` campiona HID su un thread Switch con una pausa di 4 ms e conserva in ordine le transizioni dei pulsanti. Le passate normali ed extra possono consumare nuovi report con la politica di `cellPadGetData`, che continua a restituire `len = 0` quando non c'è un report nuovo. La frequenza effettiva del thread dipende dalla schedulazione; la reattività percepita dipende anche dalla logica del gioco.

1. Ridurre il tempo della schermata esperienza/livello e ripetere la prova degli input: il miglioramento del frame time dovrebbe ridurre subito il ritardo percepito.
2. Verificare la lettura permanente senza freeze nei menu, nei duelli e nella schermata livello, controllando pressioni brevi, comandi tenuti e rilasci.
3. Nella schermata password, confrontare evento fisico, report consegnato da `cellPadGetData`, record pad del gioco (`t30-padgame.log`) e avanzamento dei frame. Se tutti avanzano ma la schermata resta bloccata, indagare la logica e il rendering della UI.
