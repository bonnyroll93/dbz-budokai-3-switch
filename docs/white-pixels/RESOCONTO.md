# Pixel bianchi a scacchiera: resoconto dell'indagine

Stato al 2026-11-19. **Bug aperto e lavoro sospeso: la correzione è stata tolta dal codice perché nei duelli provoca fasce verdi lampeggianti. Il difetto bianco è quindi di nuovo presente.** La causa precisa non è ancora isolata.

Indagine svolta dal 2026-11-17 al 2026-11-19, sessioni da run194 a run223. Lo stato attuale e il punto da cui ripartire sono nella sezione [Stato attuale e ripresa del lavoro](#stato-attuale-e-ripresa-del-lavoro).

## Sintesi

- Le attese GPU prima di copia della scena e blur eliminano il difetto in volo, ma **con la correzione compaiono fasce verdi nei duelli** (run220-223). Nel build del 17 novembre, senza attese, il duello provato è pulito (run222).
- Il motivo bianco nasce **all'uscita del blur radiale** della catena di post-processing.
- I comandi inviati alla GPU sono **identici** nei frame puliti e in quelli difettosi.
- Il difetto **sparisce se il backend invia i comandi e attende la GPU** prima dei draw che leggono una superficie: 0 frame su 900 in volo, contro 71 su 900 senza contromisure.
- Questa contromisura costa circa 6 FPS nel build di test (da 42,6 a 36,8), quindi non è la correzione definitiva.
- Una barriera GPU completa e lo svuotamento della cache a tile **non** bastano.
- Anche invalidare le cache di shader/costanti e dei descrittori **non** basta (run215).
- Attendere la GPU solo prima del blur riduce il difetto di circa 5 volte, ma non lo elimina (run214).

## Il difetto

- Compare in volo a tutta velocità nel Dragon Universe e nelle cutscene delle mosse finali, per esempio la Kamehameha di Cell.
- Il motivo copre una zona dello schermo delimitata spesso dalla diagonale, cioè un triangolo del quad a schermo intero.
- La scena resta visibile fra le barrette.
- È intermittente: in volo, senza contromisure, circa l'8% dei frame controllati.

### Forma del motivo (misurata sui dump di run200)

| Proprietà | Valore |
|---|---|
| Elemento base | barretta bianca larga 4 px e alta 16 px, a coppie separate da 4 px |
| Colore | bianco puro (255) nel 98-99% dei pixel, bordi netti |
| Griglia | barrette allineate a multipli di 4 px in orizzontale e 16 px in verticale |
| Periodo orizzontale | 64 px (la barretta si ripete a +64 px nel 96% dei casi) |
| Periodo verticale | nessuno entro 256 px: ogni riga alta 16 px ha una fase propria |
| Fra un frame e l'altro | la disposizione cambia (correlazione -0,08 fra i due dump) |

Non è quindi una texture 64x64 ripetuta sullo schermo.

## La catena di post-processing

Ricostruita dal registro degli eventi di run205. La scena alterna a ogni frame fra le superfici 1 e 4.

| Passo | Destinazione | Shader | Ingressi |
|---|---|---|---|
| 1. Riduzione | superficie 6 (640x360) | `ps_5d85b1d1de42c34e` | texture 1280x720 |
| 2. Mappa di distorsione (17 mesh) | superficie 7 (640x360) | `ps_b3104aad0182adb3` | texture 256x256 |
| 3. Copia della scena | superficie 2 (`0x026C0000`) | `ps_43b4c9959faacec2` (override) | scena, superficie 7 |
| 4. **Blur radiale a 12 campioni** | superficie 8 (`0x02AA0000`) | `ps_4f4a075bb8184542` | superficie 2 |
| 5. Blit | scena corrente | - | superficie 8 |
| 6. Ricomposizione | superficie 2 | `ps_1796b796a5a35d38` | scena corrente |
| 7. Blit finale | superficie 3 (presentata) | - | superficie 2 |

## Cronologia delle prove

| Run | Build o interruttore | Cosa abbiamo provato | Risultato |
|---|---|---|---|
| run194 | build normale | catture in volo | nessun motivo nelle catture, il difetto è intermittente |
| - | menu debug | FXAA spento | il difetto resta |
| - | `present-sync` | presentazione sincrona | il difetto resta (interruttore non verificabile nel log) |
| - | `dbz3-dof-nooverride` | shader originale del depth of field | il difetto resta |
| - | `dbz3-fsuv` | UV e quad a schermo intero | nessuna anomalia nei quad controllati |
| - | `texbatch-off` | caricamento texture sincrono | il difetto resta (interruttore non verificabile nel log) |
| run200 | `dbz3-wpwatch` | dump di tutte le superfici | motivo nelle superfici 1, 2, 3 e 8 |
| run201 | `dbz3-barrier`, nessun file | riferimento | 20 frame su 900 |
| run203 | `barrier-full` | barriera GPU completa | 5 su 450, poi crash durante una lettura di diagnostica |
| run205 | `dbz3-drawlog` | confronto eventi frame pulito/difettoso | 27 eventi identici |
| run206-207 | `dbz3-cpcheck`, `cpcheck2` | misure lungo la catena | letture pulite, motivo solo alla presentazione |
| run208 | `dbz3-cpcheck3` | misura dell'ultima catena | 11 difetti, tutti in finestre senza catena |
| run209 | `dbz3-cpcheck4` | eventi e inseguimento | **le letture a metà catena nascondono il difetto** |
| run210 | `dbz3-wpfix`, nessun file | sessione A | 71 su 900 in volo |
| run211 | `wp-tiled-flush` | svuota la cache a tile | 75 su 900 in volo, nessun effetto |
| run212 | `wp-submit-wait` | sessione breve, 25 s di volo | non conclusiva |
| run213 | `wp-submit-wait` | sessione lunga | **0 su 900 in volo** |
| run214-216 | `dbz3-wpfix2` | attese e invalidazioni separate | vedi [Prove con il build `dbz3-wpfix2`](#prove-con-il-build-dbz3-wpfix2) |
| run217 | `dbz3-wpverify` | attese prima di copia e blur | contatore 0 su 1800, a occhio difetto molto raro |
| run218 | `dbz3-wpfix2` installato per errore | test non valido | 212 su 3600, nessuna correzione attiva |
| run219 | `dbz3-wpverify2` | contatore su ogni frame, terza attesa | 1 evento, 1 lampo visto, forse dovuto a HOME |
| run220 | `dbz3-wplight` | tre attese, senza contatore | volo pulito, **fasce verdi nei duelli** |
| run221 | `dbz3-wplight2` | tolta l'attesa sulla ricomposizione | fasce verdi ancora presenti |
| run222 | build del 17 novembre | controllo senza attese | duello pulito, circa 31 s di duello nel log |
| run223 | `dbz3-wplight3` | copia limitata al volo, contatore delle attese | fasce verdi ancora presenti |

## Le scoperte principali

### I comandi sono identici (run205)

Nel confronto fra un frame pulito e uno difettoso coincidono shader, costanti, vertici, texture usate, blend, viewport e blit. Il confronto riguarda una sola coppia di frame.

### Il motivo è all'uscita del blur (run200, run207, run209)

Alla presentazione la superficie 8 ha lo stesso conteggio del motivo dell'immagine finale, in tutti i casi misurati:

| Run | Superficie 8 | Immagine finale |
|---|---|---|
| run207 | 24229 | 24229 |
| run207 | 2028 | 2028 |
| run207 | 4265 | 4265 |
| run207 | 10280 | 10280 |
| run207 | 5011 | 5011 |

### Le letture a metà catena nascondono il difetto (run209)

| Tipo di catena | Catene | Con il motivo |
|---|---|---|
| Misurata, con attesa della GPU fra i passaggi | 1795 | **0** |
| Non misurata, seguita dopo un frame sospetto | 47 | **5** |

Le 47 catene seguite non sono un campione casuale, quindi il loro 11% non è la frequenza reale. Questo risultato spiega perché i punti di controllo di run206-208 risultavano sempre puliti.

### Il confronto finale (build `dbz3-wpfix`)

Sono confrontate le finestre 2 e 3 del contatore, in volo. La prima finestra comprende avvio e menu, quindi è esclusa.

| Sessione | Run | Interruttore | Frame col motivo | FPS in volo (mediana) |
|---|---|---|---|---|
| A | run210 | nessuno | 71/900 | 42,6 |
| B | run211 | `wp-tiled-flush` | 75/900 | 42,6 |
| C | run213 | `wp-submit-wait` | **0/900** | **36,8** |

Gli FPS valgono per il build di test, che legge l'immagine presentata ogni 4 frame.

## Cosa è escluso

| Ipotesi | Prova | Affidabilità |
|---|---|---|
| FXAA | difetto presente con AA spento | buona |
| Override del depth of field | difetto presente con lo shader originale | buona |
| Cache a tile | run211 uguale alla sessione senza interruttori | buona |
| Draw in più o parametri diversi nella catena | run205 | media: una sola coppia di frame |
| Due catene fra due presentazioni | run208: 45 frame con più catene, nessuno difettoso | media |
| Barriera GPU mancante fra i passaggi | run203: il motivo resta | media: una sola finestra, poi crash |
| Presentazione asincrona | `present-sync` | bassa: interruttore non registrato nel log |
| Caricamento texture a lotti | `texbatch-off` | bassa: interruttore non registrato nel log |

## Cache invalidate a inizio lista (ipotesi esclusa da run215)

`dk_submit_wait` fa più di una barriera. Alla fine chiama `dk_cmd_reset`, che all'inizio di ogni nuova lista di comandi invalida quattro cache:

| Cache invalidata a inizio lista | Barriera prima dei draw che leggono una superficie | Barriera completa |
|---|---|---|
| Texture (`Image`) | sì | sì |
| L2 | no | sì |
| **Shader e costanti (`Shader`)** | **no** | **no** |
| **Descrittori (`Descriptors`)** | **no** | **no** |

Il codice stesso segnala il rischio: descrittori, codice shader e ring dei dati vengono scritti dalla CPU fra un invio e l'altro. Descrittori di texture e sampler possono però essere scritti anche a metà di una lista. Invalidando insieme le due cache in grassetto prima dei draw che leggono una superficie, il difetto resta (run215), quindi non sono loro la causa.

Restano da distinguere gli altri effetti di `dk_submit_wait`: l'attesa della GPU, l'invio dei comandi, il riuso del ring dei dati e il nuovo binding di tutto lo stato a inizio lista.

## Prove con il build `dbz3-wpfix2`

| Run | Interruttore | Frame col motivo in volo | FPS in volo (mediana) |
|---|---|---|---|
| run214 | `wp-wait-blur` | 19/1350 (finestre 3-5), circa 1,4% | 41,6 |
| run215 | `wp-inv-shader` + `wp-inv-desc` | 62/900 (finestre 2-3), circa 6,9% | 42,4 |
| run216 | `wp-wait-blur` + `wp-wait-copy` | **0/450** (finestra 2), sessione di 86 s | circa 41,5 |

In run216 il difetto non si è più visto a occhio e il contatore resta a zero. Nella stessa finestra le sessioni senza correzione efficace avevano da 25 a 33 frame col motivo. Il dato copre però una sola finestra in volo e non comprende le mosse finali.

L'attesa prima del solo blur riduce il difetto di circa 5 volte rispetto alla sessione A (7,9%), ma non lo elimina. In run214 il volo inizia dopo circa 60 secondi, quindi le prime due finestre sono escluse. L'invalidazione delle due cache non ha un effetto apprezzabile.

## Verifiche della correzione

La correzione provata attendeva la GPU prima di `ps_43b4c9959faacec2`, `ps_4f4a075bb8184542` e `ps_98272f5dc7f7c421`.

**run217, build `dbz3-wpverify`, circa 170 secondi di log.** Il contatore segna 0 frame col motivo su 1800 controllati e il volo gira a 42,4 FPS, contro 42,6 senza correzione. A occhio il difetto compare ancora, molto di rado. Il contatore controllava un frame ogni 4 e ignorava i motivi sotto la soglia di 2000.

**run219, build `dbz3-wpverify2`.** Il contatore legge ogni frame e c'è una terza attesa prima di `ps_1796b796a5a35d38`.

- Il log contiene 1 solo evento (22025 tratti bianchi, un frame) su circa 6000 frame di volo con il blur. Coincide con l'unico lampo visto.
- L'evento è accanto all'unico blocco della sessione, di circa 5,4 secondi. L'utente pensa di aver premuto HOME in quel momento, senza esserne certo.
- Il volo gira a 37,5-39 FPS, ma la lettura di ogni frame rende il dato non confrontabile con gli altri build.

**run218 non è valido:** sulla Switch era installato `dbz3-wpfix2`. Prima di leggere un test, controllare la riga `[build] compiled` del log.

## Le fasce verdi nei duelli

Con il build giocabile `dbz3-wplight` il volo è pulito a occhio, ma nei duelli compaiono a tratti fasce verdi lampeggianti. I log non contengono errori grafici.

| Run | Attese attive | Fasce verdi |
|---|---|---|
| run220 | copia, blur `4f4a`, blur `98272`, ricomposizione `1796` | presenti |
| run221 | copia, blur `4f4a`, blur `98272` | presenti |
| run222 | nessuna (build del 17 novembre) | **assenti**, circa 31 s di duello nel log |
| run223 | copia solo dopo un blur `4f4a` recente, blur `4f4a`, blur `98272` | presenti |

Attese contate in run223, per ogni tratto di 900 frame:

| Tratto | Copia | Copia saltata | Blur `4f4a` | Blur `98272` |
|---|---|---|---|---|
| 1 | 0 | 0 | 0 | 0 |
| 2 | 0 | 0 | 0 | 0 |
| 3 | 194 | 289 | 152 | 0 |
| 4 | 71 | 642 | 43 | 0 |
| 5, arriva al duello | 52 | 466 | 37 | **648** |

**Sospetto principale, non confermato: l'attesa prima di `ps_98272f5dc7f7c421`.** Scatta in massa solo nel tratto che arriva al duello, mentre in run219 quel blur non è mai stato disegnato in volo. Il meccanismo che produce il verde è sconosciuto. Il log di run223 si ferma a 87 secondi, con pochi secondi di duello.

## Stato attuale e ripresa del lavoro

Il 2026-11-19 si è deciso di sospendere la correzione.

- **Codice:** attese GPU, rilevatore e contatori sono stati tolti da `rsx_deko3d_backend.c`. Le modifiche non sono state committate.
- **Patch salvata:** l'ultima versione provata, quella di run223, è in `deko3d-backend-wpfix-gated-waits.patch`. La patch è calcolata rispetto all'ultimo commit, quindi contiene anche le altre modifiche non committate del file.
- **Build da usare:** `build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp` del 17 novembre, l'ultimo provato senza fasce verdi.
- **Build di controllo:** `build/dev/dbz3/dbz3-nofix.nsp` è compilato dal codice ripristinato, ma non è stato provato sulla Switch.

**Da dove ripartire:**

1. Riapplicare la patch e togliere solo l'attesa prima di `ps_98272f5dc7f7c421`.
2. Provare nella stessa sessione almeno 3-5 minuti di duello, 5 minuti di volo senza premere HOME e una mossa finale.
3. Se le fasce restano, capire quale altra attesa scatta nei duelli. Se spariscono, controllare che i pixel bianchi non tornino nelle mosse finali.

La causa profonda nella GPU resta da spiegare.

## Interruttori del build `dbz3-wpfix2`

Sono salvati in `deko3d-backend-wpfix-switches.patch`:

| File sull'SD | Cosa fa | Cosa ci dice |
|---|---|---|
| `wp-wait-blur` | attende la GPU solo prima del blur | se basta una sola attesa per frame |
| `wp-wait-copy` | attende la GPU solo prima della copia della scena | se il problema nasce un passaggio prima |
| `wp-submit-only` | invia i comandi senza attendere | se conta l'invio e non l'attesa |
| `wp-inv-shader` | invalida la cache di shader e costanti | se la causa sono costanti vecchie |
| `wp-inv-desc` | invalida la cache dei descrittori | se la causa sono descrittori vecchi |

Ogni sessione richiede almeno 3 minuti di volo. Si confrontano le finestre del contatore dopo la prima e gli FPS in volo con la sessione A.

## Regole per misurare questo difetto

- **Non leggere le superfici a metà catena:** l'attesa della GPU nasconde il difetto.
- **Ignorare la prima finestra del contatore:** comprende avvio e menu.
- **Volare almeno 3 minuti:** sessioni più brevi non chiudono abbastanza finestre.
- **Verificare gli interruttori nella riga `sd switches` del log.**

## Altri difetti emersi

- **Immagine leggermente doppiata in velocità:** ancora da capire. Il blur usa 12 copie con lo stesso peso, distanziate fino a circa 4,5 px vicino ai bordi, quindi potrebbe essere l'effetto originale. Mancano una foto e la conferma di dove si vede.
- **Freeze contro Majin Bu (run204):** il thread principale resta in attesa in `func_002909C0`, come nel freeze del 7 novembre. Le righe del FIFO sono andate perse perché il gioco è stato riavviato prima di scaricare i log.

## File utili

| File o cartella | Contenuto |
|---|---|
| `docs/white-pixels/fs-uv-check.patch` | controllo UV in `rsx_draw_engine.c`, tolto dal codice |
| `docs/white-pixels/deko3d-backend-wp-watch-and-barrier.patch` | rilevatore, dump e `barrier-full` |
| `docs/white-pixels/deko3d-backend-checkpoints.patch` | registro eventi e punti di controllo |
| `docs/white-pixels/deko3d-backend-wpfix-switches.patch` | interruttori sull'SD del build `dbz3-wpfix2` |
| `docs/white-pixels/deko3d-backend-wpfix-gated-waits.patch` | ultima correzione provata (run223), con rilevatore e contatori |
| `build/logs/dbz3/dev/run200/captures/` | dump completi di due frame col motivo |
| `build/logs/dbz3/dev/run205/` | confronto degli eventi |
| `build/logs/dbz3/dev/run209/` | prova che le letture nascondono il difetto |
| `build/logs/dbz3/dev/run210/`, `run211/`, `run213/` | sessioni A, B e C |
| `build/logs/dbz3/dev/run219/` | contatore su ogni frame, un solo evento |
| `build/logs/dbz3/dev/run220/` ... `run223/` | prove sulle fasce verdi nei duelli |
| `userdata/screenshoot/bug_whitepixels/` | foto dello schermo |
