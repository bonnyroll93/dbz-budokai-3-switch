# Dragon Universe: shader e prestazioni, run07

## Fonti e limiti

- Foto: `userdata/screenshoot/IMG_20261026_071207712.jpg` e `IMG_20261026_071223161.jpg`.
- Raccolta Switch: `build/logs/dbz3/dev/run07`, build compilata il 26 ottobre 2026 alle 09:08:05, con input senza freeze e campionatore PPU attivo.
- Le foto mostrano circa 25,6 e 21,7 FPS. Non sono sincronizzate con i singoli frame dei log.
- Il task `0x001DBC58` gestisce mappa e altre schermate di Dragon Universe: la sua presenza identifica il percorso DU, ma da sola non distingue ogni schermata.
- `t22-gpu.log` viene accodato: i primi 151040 byte di run07 sono identici a run06. Le segnalazioni di shader mancanti sono in quella parte storica; nei 2093 byte aggiunti non ce ne sono di nuove. Anche `t23-shaders/pipelines.txt` è identico a run06.

## Prestazioni

Finestre di `t26-loop-prof.log` con il task DU presente una volta per passata regolare. La numerazione parte dalla prima riga `window_ms=`. Nonostante il nome, quel campo contiene **microsecondi**, come conferma `flush()` in `dbz3_loop_prof.cpp`.

| Finestra | Passaggi/s | Tempo medio passata | Task DU regolare | Passate extra complessive per immagine |
| --- | ---: | ---: | ---: | ---: |
| 11 | 25,17 | 39,73 ms | 17,54 ms | 16,04 ms |
| 12 | 25,70 | 38,92 ms | 17,48 ms | 15,12 ms |
| 14 | 22,28 | 44,88 ms | 19,52 ms | 20,05 ms |
| 15 | 21,26 | 47,04 ms | 19,39 ms | 21,73 ms |
| 20 | 13,91 | 71,90 ms | 21,13 ms | 42,57 ms |

L'ultima colonna deriva da `extra_avg_us * (passes_2 + 2*passes_3 + 3*passes_4) / passes`. Include tutti i task extra, non soltanto il disegno DU. I passaggi del ciclo principale non sono una misura di GPU frame time; `t21-clock.log` conferma comunque cali nelle finestre di presentazione finali (30 frame in 1145–2234 ms).

### Costo principale individuato

`extra_logic_step()` ripete il task `0x001DBC58`, che contiene sia aggiornamento sia disegno. Il codice ripristina poi il puntatore GCM per scartare i comandi prodotti: risparmia l'esecuzione di quei comandi nel renderer, ma paga già il lavoro CPU necessario a prepararli.

Nella finestra 12 ci sono 104 ripetizioni annullate, con circa 17 MiB di comandi scartati. Nella finestra 20 sono 137, con circa 25,5 MiB scartati. Al calare degli FPS aumentano i passi da recuperare e il loro costo; nell'ultima finestra la logica complessiva arriva soltanto a 54,8 passi/s.

Inoltre `no_room` registra ripetizioni DU saltate perché il buffer non ha spazio sufficiente: 68 nella finestra 12, 69 nella 20. Il contatore `logic60` misura i passi globali e non garantisce che il task DU sia stato eseguito in ciascuno di essi.

Nell'ultima finestra del campionatore, il task DU raccoglie 4039 campioni su 4871. Le funzioni più frequenti al suo interno includono `000328A8` (364), `000252B0` (287), `0001F1E8` (203), `0002C088` (200), `0001AA00` (180). `0001F1E8` trasforma un punto mediante una matrice. Il costo è distribuito: la sola ottimizzazione dell'ordinamento della schermata livello non affronta questo percorso.

Gli ultimi picchi di `t29-spikes.log` durano circa 70–76 ms con zero attese/fence GPU, upload, readback e nuove pipeline registrati. Questo indirizza prima sul lavoro CPU; non misura direttamente quanto tempo impiega ogni shader sulla GPU.

### Intervento prioritario per 30 FPS

1. Misurare separatamente i gruppi di chiamate del task DU nella passata regolare e in quella extra. Il blocco finale da `loc_001DE4F4` contiene una sequenza di chiamate da classificare, non ancora dimostrata priva di effetti sulla logica.
2. Individuare un confine sicuro tra aggiornamento e produzione dei comandi grafici. Eseguire l'aggiornamento nei passi extra e produrre il disegno una volta per immagine, preservando timer, movimento, menu e transizioni. Evitare di saltare tutto il task: il codice precedente segnala già un rallentamento della mappa con quella soluzione.
3. Ottimizzare poi le trasformazioni e la preparazione dei materiali rimaste frequenti, conservando la semantica numerica guest.
4. Confrontare gli stessi percorsi terra/acqua e la schermata esperienza: obiettivo 33,3 ms per immagine, anche nelle finestre peggiori. Ripetere la misura senza il campionatore PPU per quantificarne l'incidenza.

## Shader

### Osservazioni dalle foto

- Pelle/braccia bianche sia sopra la terra sia sopra l'acqua: il difetto di colore non dipende soltanto dalla posizione sopra l'acqua.
- Nella foto sull'acqua una deformazione orizzontale interessa la sagoma del personaggio e parti dello sfondo. Serve una cattura dei passaggi intermedi per attribuirla al rendering dell'acqua.

### Copertura completata

Importati byte per byte dai file raccolti i due vertex shader ancora assenti dal progetto:

- `vs_1f371557f87a7ad2.hlsl`, usato nella cattura con `ps_511e3e7252f5f9dc`.
- `vs_dcad899064a035c0.hlsl`, usato nella cattura con `ps_634d94e85f949cd6`.

Il generatore verifica l'hash dei sorgenti durante l'importazione. Compilazione uam: **47 shader, zero errori**. Queste aggiunte coprono combinazioni storicamente rifiutate; non costituiscono una correzione dimostrata dei difetti fotografati.

### Piste da verificare

- Lo shader corpo `ps_511e3e7252f5f9dc` dispone già di un override con rami fuori schermo introdotti per indagare la pelle bianca. La sua esistenza non prova che il difetto sia risolto. Occorre associare il materiale bianco nella mappa al suo shader effettivo, alla texture, ai colori/normali interpolati e alle costanti del draw.
- `t28-tex/index.txt` registra il corpo con dieci costanti ai frame 1416–1572; più avanti registra molte texture della mappa con il programma guest `0x01A68D40`, quattro costanti. Non è quindi corretto attribuire automaticamente tutti i materiali DU allo shader corpo.
- `ps_43b4c9959faacec2` contiene un percorso di distorsione: usa la texture 1 per spostare le coordinate con cui campiona la texture 0 e il suo canale Z per decidere se applicare lo spostamento. È un candidato da confrontare con il draw effettivo dell'acqua, non ancora un'associazione provata con la foto.
- Il backend Switch ha `depth_snapshot()` non implementato e nessun `depth_readback`. È un limite da verificare qualora il passaggio incriminato legga una superficie depth; i log attuali non dimostrano che sia la causa.

La prossima cattura grafica deve mostrare i target prima e dopo la distorsione, gli shader e le texture dei draw del personaggio, e lo stato depth/blend. Il draw engine dispone già di `RSX_ENGINE_PASS_DUMP` e `RSX_ENGINE_PASS_DUMP_DRAWS`; la cattura va attivata quando la scena interessata è visibile, con un limite di passaggi. Le misure FPS vanno raccolte separatamente, perché il readback altera i tempi.

## Stato

Build `du-shader-coverage` compilata e pacchettizzata il 26 ottobre 2026 alle 09:25:56 in `build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp`, con campionatore PPU attivo. Non ancora installata né verificata visivamente sulla console in questa indagine.

Input senza freeze mantenuti. Due shader aggiunti; pelle bianca e distorsione ancora aperte. Nessuna nuova ottimizzazione FPS applicata in questa indagine: individuati e quantificati il task DU e il costo delle passate extra. I 30 FPS stabili restano da raggiungere.

### Profilo delle fasi del task DU

La build diagnostica successiva inserisce quattro sonde nel solo task `0x001DBC58`, senza modificare registri o memoria guest. In `t26-loop-prof.log`, `du-phase pass=regular/extra` riporta per ogni fase il numero di esecuzioni, il tempo medio e i KiB medi di comandi GCM prodotti:

Build `du-phase-profile` compilata e pacchettizzata il 26 ottobre 2026 alle 10:05:29 nello stesso percorso NSP di sviluppo. I valori delle nuove sonde saranno disponibili dopo l'installazione e una sessione sulla mappa.

- `p0`: ingresso del task fino al punto comune `loc_001DE4F4`.
- `p1`: da quel punto fino alla chiamata guest `0x001DE598`.
- `p2`: dalla chiamata fino all'uscita dal task.

Le quantità di comandi consentono di individuare dove avviene il disegno; i tempi confrontano la stessa parte nelle passate normali ed extra. Serve una raccolta Switch della mappa per scegliere un taglio sicuro tra aggiornamento e disegno.

### Esito run08 e intervento

Build sperimentale `du-update-only` compilata e pacchettizzata con successo il 26 ottobre 2026 alle 10:18:33: `build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp`. Il guadagno di FPS e il comportamento di movimento, menu e transizioni devono ancora essere verificati sulla Switch. La compilazione non costituisce una conferma dei 30 FPS.

La Switch ha eseguito `du-phase-profile`. Nelle finestre DU, `p0` in passata extra richiede 0,43–0,46 ms e produce 0 KiB GCM. `p1` richiede circa 8,7–14,4 ms con 47–87 KiB; `p2` circa 5,6–11,2 ms con 105–136 KiB. I valori della passata regolare sono simili. Per esempio nella finestra da 95 passaggi in 5,035 secondi, 139 ripetizioni extra producono 21.586 KiB poi annullati; 70 altre ripetizioni sono saltate per mancanza di spazio. La finestra scende a 18,9 passaggi/s. `t29-spikes.log` continua a mostrare picchi senza attese GPU rilevanti nella mappa.

La modifica successiva esegue `p0` in ogni passo logico DU e salta `p1` e `p2` soltanto nelle passate extra. La passata regolare continua a disegnare. Con questo percorso la preparazione GCM ripetuta non dovrebbe consumare spazio nel ring; il contatore `du-render-skipped` rende visibile quante volte è stato applicato il taglio. Per confronto o regressioni, creare `sdmc:/switch/dbz-budokai-3-switch/du-full-extra` prima di avviare il gioco ripristina la passata extra completa. La bontà funzionale della separazione resta da verificare sulla console: 0 KiB GCM in `p0` prova soltanto che non scrive nel buffer dei comandi, non che ogni effetto collaterale di `p1` e `p2` sia grafico.
