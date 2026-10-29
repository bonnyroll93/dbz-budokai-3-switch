# ROAD to 60 FPS

## Obiettivo

60 presentazioni/s stabili (16,67 ms per frame) in **Duello**, **mappa Dragon Universe (DU)** e **schermata esperienza/livello** dopo un combattimento DU, con la logica a 59–61 Hz e senza regressioni di input o di resa grafica.

Il lavoro precedente puntava a 30 FPS. Il budget ora si dimezza: ogni ms conta il doppio e il lavoro CPU per frame va portato a circa 14 ms nei casi pesanti (il resto è margine per sincronizzazione, audio e variazioni di carico).

## Regole di lavoro

1. **Si riparte da misure nuove.** I numeri dei vecchi `run03`–`run30` (documentati nella storia git, `docs/ROAD_MAP30fps.md`) sono usati solo come ipotesi, non come baseline: build, scene e finestre erano diverse e alcuni file di log erano stantii.
2. **Una modifica per build**, con un nome `-Feature` univoco e un contatore che deve cambiare.
3. **Si misura con la build leggera** (`Lightweight = $true`, solo `Logic60` attivo). I profiler alterano il tempo: servono per localizzare, mai per dichiarare l'FPS.
4. Ogni log si confronta con la baseline **solo** se ha lo stesso scenario, la stessa durata e lo stesso `build-info.txt`. Controllare l'hash di `t23-ppu-hot.log` e simili: un file identico al run precedente è rimasto sulla SD ed è da scartare.
5. Una modifica resta solo se migliora il frame time senza rompere logica a 60 Hz, input e immagine (confronto con draw capture dove serve).

## Fase 0: nuova baseline

Scopo: sapere con precisione dove si perdono i ms oggi, su tre scenari ripetibili.

**Scenari (60 s ciascuno, stesso punto di partenza dopo ogni riavvio):**
- A. Duello (stessi personaggi, stessa arena, combattimento attivo).
- B. Mappa DU (stesso punto, ferma e in movimento/volo).
- C. Schermata esperienza/livello dopo un combattimento DU.

**Passi:**
1. Build di riferimento `base60-light`: configurazione di default (leggera). Registrare in `build-info.txt` hash ELF e NSP. Dal menu debug leggere FPS e frame ms su ogni scenario, 3 ripetizioni, e annotare min/mediana/max.
2. Build diagnostica `base60-prof`: stesso codice, con `LoopMetrics`, `FrameClockMetrics` e `ProfilePpuFunctions`. Un giro per scenario, log scaricati con `scripts/pull-switch-logs.ps1`.
3. Compilare una tabella per scenario (modello sotto) e salvarla in questo file.

| Scenario | FPS (leggera) | Presentazioni/s | Passi logici/s | Task guest dominante (ms) | Draw/frame | µs CPU/draw | Attese GPU |
|---|---|---|---|---|---|---|---|
| A Duello | 30,9 (AVG 31, MIN 29,4, MAX 33,7) | ~31 (frame 32 ms) | 60,0 | `0x000D2310` 10,2 ms; task guest 27 ms + 3 ms extra per passata | ~350 per render | fifo 22 ms/frame | trascurabili |
| B Mappa DU | 31 (AVG 30, MIN 30,9, MAX 32) | ~30 (frame 33 ms) | 60,0 | `0x001DBC58` 16,3 ms; task guest 20 ms per passata | ~210–250 per render | fifo 19–20 ms/frame | trascurabili |
| C Livello | 19 (MIN 13 dai log) | 13–19 (frame 53–77 ms) | 44–60 (cala sotto 60) | `0x001D1604` 64 ms; task guest 74 ms per passata | 700–1100 per render | fifo 36–48 ms/frame | picchi isolati |

Prime misure dell'utente (29 ottobre 2026) con la build `base60-light` (leggera, `ScreenshotCapture` attivo), dal contatore FPS e dalle statistiche del debug menu. Le catture del menu non includono l'overlay, quindi i valori sono stati riportati a mano. Le colonne vuote si riempiono con la build `base60-prof`.

Lettura: duello e mappa DU stanno stabilmente intorno a 30 FPS, con una dispersione molto piccola (MIN e MAX a meno di 3 FPS dalla media). Nel codice non ho trovato un limitatore esplicito a 30 FPS (nessun `SwapInterval`, né divisore di presentazione). Più probabile la quantizzazione del vsync a 60 Hz: un frame che costa tra 16,7 e 33,3 ms viene presentato al secondo VBlank e produce sempre circa 30 FPS. Quindi l'FPS misurato non dice quanto si è lontani dai 60: il tempo reale per frame può essere ovunque tra 17 e 33 ms. Serve il costo CPU e GPU per frame (build `base60-prof`, `FrameClockMetrics`) prima di scegliere gli interventi. La schermata livello è il caso peggiore a 19 FPS.

4. **Chiave diagnostica: il collo di bottiglia è CPU o GPU?** Test con render scale ridotta (già fatto in passato con esito "non cambia", da ripetere sulla nuova build) e confronto tra tempo CPU del frame e tempo GPU. Per la GPU aggiungere un timestamp deko3d per frame (query di tempo) se non basta `FrameClockMetrics`.
5. **Chiave diagnostica: come sono distribuiti i thread?** Con `switch_thread_stats` ricavare il carico per core. Il percorso guest (PPU lifted) e il draw engine lavorano su thread diversi? Quanto è serializzato?

**Uscita Fase 0:** per ciascuno scenario, un budget di frame ripartito in: PPU guest, preparazione draw (draw engine), submit/attese GPU, sync/audio/altro. Le fasi successive si ordinano per ms recuperabili in quella tabella.

## Esito Fase 0 (run57, build `base60-prof`, 29 ottobre 2026)

Sessione di 236 s: DU mappa (circa 25 s), duello (circa 130 s), schermata livello (circa 40 s). Le scene sono separate con i task guest (`0x001DBC58` mappa DU, `0x001D1604` livello, resto duello/menu) e allineate ai log del clock per tempo cumulativo, con un errore possibile di una finestra da 5 s. La build ha i profiler attivi: i tempi assoluti sono un po' gonfiati, le proporzioni valgono. Script di analisi: `scripts/analyze-run.py`.

**1. Non è la GPU.** `t29-spikes.log` registra attese GPU solo in 12 picchi su 744 e `fence_ms` è sempre 0. Il collo di bottiglia è la CPU, su due thread in parallelo.

**2. Due thread, entrambi sopra il budget dei 16,7 ms.**

| | Thread guest (PPU lifted) | Thread FIFO/renderer |
|---|---|---|
| Duello | ~30 ms per passata di 2 passi logici (27 ms regolare + 3 ms extra), quasi saturo | ~22 ms per frame |
| Mappa DU | ~20 ms per passata, più ~13 ms di attesa | ~19–20 ms per frame |
| Livello | ~74–80 ms per passata | ~36–48 ms per frame |

Il vsync a 60 Hz arrotonda a 2 VBlank (33,3 ms) qualunque frame tra 16,7 e 33,3 ms: per questo i FPS stanno incollati a 30. Per i 60 FPS servono **entrambi** i thread sotto ~14 ms per frame, con margine.

**3. Il renderer costa 35–47 µs per draw.** In duello, circa 16,5 ms per render su ~350 draw: pipeline 25%, vertex fetch 20%, texture 23% (di cui hash dei dati texture 9%), draw backend 21%, costanti 7%. L'hash texture rilegge 300–440 MB ogni 5 s. Una cache pipeline e vertex ben fatta dovrebbe stare sotto 10 µs per draw.

**4. La schermata livello fa 700–1100 draw per frame** per una UI 2D. È probabile che molti siano elementi di lista fuori dal riquadro visibile. Qui la logica scende anche sotto 60 Hz (44–50 passi/s), quindi il gioco rallenta, non solo i FPS.

**5. Passata regolare contro passo extra.** Oggi il lavoro pesante (simulazione più emissione draw) gira una volta ogni 2 passi logici; il passo extra costa solo 3 ms. A 60 FPS ogni frame sarà una passata regolare: il duello va da 27 ms a meno di ~14 ms per passata, cioè quasi dimezzato.

**Task e funzioni guest dominanti**
- Duello: task `0x000D2310` (10,2 ms), `0x00094DD8` (3,7), `0x000700D4` (3,0), `0x0006FDF8` (2,8). Funzioni calde: `0x000328A8`, `0x0002C088`, `0x000252B0`, `0x0004FB74` (ognuna 3–4% dei campioni, quindi costo molto distribuito).
- Mappa DU: `0x001DBC58` 16,3 ms (84% dei task); `0x00159BE8` (34% dei campioni, probabile attesa e non lavoro).
- Livello: `0x001D1604` 64 ms, con `0x00030790` (12%), `0x0005A098`, `0x0002F56C`, `0x0004A118`.

**Priorità che ne derivano**
1. **Renderer (Fase 2)** guadagno più chiaro: 22 ms → meno di 14 ms nel duello. Sono il pipeline lookup, il vertex fetch e l'hash texture. Meno draw (scarto di draw fuori scissor o a zero pixel) aiuta soprattutto la schermata livello.
2. **Guest (Fase 3)**: nel duello il costo è sparso su molti task, quindi servono guadagni generali (compilatore, qualità del codice lifted, affinità dei thread) più che una singola funzione. La mappa DU è un caso a parte: un solo task da 16 ms.
3. **Fase 1** (`-O2`/`-O3`, affinità dei thread) è il primo passo perché potenzialmente dà un guadagno su tutto senza costi di batteria, e va misurato su `base60-light`.
4. **Pipeline parallela guest/renderer** (2.1) è già il design di oggi (due thread); il margine è quanto più si riesce a sovrapporli, ma con entrambi sopra 16,7 ms da soli non basta.

## Fase 1: guadagni a basso rischio (da validare con la baseline)

Non toccano la correttezza del codice lifted né la sequenza dei draw.

| ID | Ipotesi | Verifica | Rischio |
|---|---|---|---|
| 1.1 | **Overclock/boost del clock: scartato.** Il gioco gira al clock normale (CPU 1020 MHz) e non si alza, per non pesare sulla batteria. Ogni guadagno deve venire da codice più efficiente. | | |
| 1.2 | **Ottimizzazione compilatore**: il target usa `-O1` per le TU PPU enormi, solo alcune a `-O2`. Verificare `-O2`/`-O3` sulle TU più calde (dal profilo PPU), LTO/`-fno-plt`, `-mcpu=cortex-a57+crypto`, `-ffast-math` solo per le parti non vettoriali. | Tempo del task dominante, dimensione binario | Medio: tempi di build lunghi; verificare che la semantica float non cambi |
| 1.3 | **Affinità e priorità dei thread**: assegnare esplicitamente thread guest, draw engine e frame clock a core diversi (la Switch dà 3 core ai giochi) ed evitare che audio/input/IO competano con quello principale. | Carico per core, frame time | Basso |
| 1.4 | **Costo per draw**: lavori già fatti (cache attributi VP, indice pipeline, texture state). Rimisurare cosa resta in `pipeline`, texture e vertex upload con il profilo nuovo. | µs/draw per fase | Basso |
| 1.5 | **Audio/I/O**: verificare che il thread audio e le letture da SD/RomFS non blocchino mai il percorso del frame. | Picchi in `t29-spikes.log` | Basso |

## Fase 2: ridurre il lavoro del renderer

Se la Fase 0 mostra che le draw (centinaia per frame, picchi di ~800–900 nella schermata livello secondo i vecchi log) pesano:

| ID | Intervento | Note |
|---|---|---|
| 2.1 | **Renderer su thread dedicato**: il guest produce il command buffer GCM, un secondo thread lo traduce e invia a deko3d, in pipeline con il frame successivo. Può dimezzare il tempo sul thread principale. Difficoltà: sincronizzazione di memoria guest, semafori RSX, report. | Il più grande potenziale, il più delicato |
| 2.2 | **Unione e riduzione delle draw**: raggruppare draw con stesso stato/pipeline; evitare state change ridondanti; riuso di vertex buffer e UBO per frame. | Misurare quante draw ripetono lo stesso stato |
| 2.3 | **Cache di trasformazioni vertex**: già presente una vertex cache; verificarne hit rate e dimensioni. | |
| 2.4 | **Texture**: upload e conversioni (BC2 via RGBA8 è un compromesso) da rivedere se compare nel profilo. Ridurre i controlli di hash per draw. | |
| 2.5 | **Command buffer deko3d**: registrare più comandi per submit; ridurre fence e flush; valutare `present-sync` e `flush512` con la nuova baseline. | Modifica di comportamento visibile solo nei tempi |
| 2.6 | **Salto di lavoro invisibile**: draw fuori schermo o a zero pixel, passaggi (blur, FXAA già opzionale) disattivabili per scenario. | Possibile degrado visivo, a scelta dell'utente |

## Fase 3: ridurre il lavoro del guest (PPU lifted)

Dal profilo PPU si scelgono le poche funzioni che dominano, poi:

| ID | Intervento | Note |
|---|---|---|
| 3.1 | **Nativizzazione di funzioni calde** (come `dbz3_ppu_native.cpp`): sostituire loop interni con C++ nativo equivalente. Precedente: `level-sort-scalar` ha ridotto il task livello da ~140–156 ms a ~58–65 ms; `level-list-native` non ha dato benefici ed è stato rimosso. Lezione: misurare prima, e puntare ai corpi delle funzioni, non ai loop esterni. | |
| 3.2 | **Qualità del codice lifted**: vettori AltiVec tradotti con NEON invece di accessi lane per lane, e meno load/store su `ctx`; verificare l'ordine dei byte (storia: bug vmul/vsubsws). Ogni modifica va confrontata bit a bit sulle uscite note. | Alto rischio di regressione, test dedicati |
| 3.3 | **Schermata livello**: il task `0x001D1604` resta il caso peggiore storico. Capire quanti draw sono statici e se il loro risultato si può riutilizzare tra passi logici. | |
| 3.4 | **Mappa DU**: il task `0x001DBC58` fa insieme logica e disegno; la modalità `du-update-only` nei passi extra scarta già il disegno. Con 60 FPS i passi extra spariscono (un passo per frame), quindi il costo da ottimizzare è quello della passata regolare. | |
| 3.5 | **SPU**: verificare con `ProfileSpuFunctions` se i lavori SPU emulati occupano un core e a quale priorità girano. | |

## Fase 4: scheduler a 60 Hz

Con 60 presentazioni/s, la logica gira 1:1 con il rendering. Rivedere `Logic60`:
- eliminare i passi logici extra tra frame renderizzati quando non servono più;
- verificare che il pad sia letto una volta per passo, senza latenza aggiunta;
- mantenere l'accumulatore VBlank a 60,00 Hz e verificare il pacing con `FrameClockMetrics` (nessun frame doppio o saltato; vsync coerente con 60 Hz del display).

## Fase 5: verifica finale

- Sessioni di almeno 5 minuti per scenario: presentazioni ≥ 60/s per il 99% delle finestre da 1 s, logica 59–61 Hz.
- Controlli di input (comandi mancati/ritardati), animazioni, cutscene e finishing move in sincrono, immagine stabile (confronto con draw capture).
- Modalità portatile e dock, batteria e temperature.
- Se 60 pieni non sono raggiungibili in tutti gli scenari: definire un fallback (Duello 60, DU map e schermata livello 30 con pacing stabile) e documentarlo nel README.

## Ordine proposto

1. Fase 0 (nuova baseline) e tabella compilata.
2. 1.1, 1.2, 1.3 in build separate: sono economiche e dicono quanto margine resta.
3. In base alla tabella: Fase 2 (se domina il renderer) o Fase 3 (se domina il guest).
4. Fase 4 e Fase 5.

## Registro delle prove

| Build (`-Feature`) | Modifica | Scenario | Prima | Dopo | Esito |
|---|---|---|---|---|---|
| `opt1-prof` (run58 contro run57) | Costanti fragment: copia dei soli slot usati invece del blocco da 32 KB; cache vertex program per contenuto; span texture solo su miss | Duello | renderer 47,6 µs/draw, 16,4 ms/render | 36,5 µs/draw, 11,9 ms/render | Tenuta. Fase pipeline 11,8 → 2,4 µs/draw |
| | | Mappa DU | 67,7 µs/draw, 15,1 ms/render | 55,8 µs/draw, 12,5 ms/render | Tenuta |
| | | Livello | 34,1 µs/draw, 32,8 ms/render | 24,3 µs/draw, 24,9 ms/render | Tenuta. FPS ancora limitati dal thread guest (passata 87–90 ms) |
