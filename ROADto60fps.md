# ROAD to 60 FPS

## Obiettivo

60 presentazioni/s stabili (16,67 ms per frame) in **Duello**, **mappa Dragon Universe (DU)** e **schermata esperienza/livello** dopo un combattimento DU, con la logica a 59–61 Hz e senza regressioni di input o di resa grafica.

Il lavoro precedente puntava a 30 FPS. Il budget ora si dimezza: ogni ms conta il doppio e il lavoro CPU per frame va portato a circa 14 ms nei casi pesanti (il resto è margine per sincronizzazione, audio e variazioni di carico).

## Stato al 2 novembre 2026

**Traguardo raggiunto nel duello: 60 FPS al clock normale.** Run139, Goku contro Vegeta, build metrics: circa 300 immagini ogni 5 s per tutto il duello, nessun ritorno a 30 Hz, fluido a vista (commit `e752cba`). All'inizio (29 ottobre, `base60-light`) il duello stava a ~31 FPS.

| Passo | Run | Effetto nel duello |
|---|---|---|
| Funzioni matematiche native, scrittori dello stato di disegno | run89–92, run105 | circa −3 ms per immagine sul thread del gioco |
| Nuovo regolatore 30/60, tre immagini in volo, triplo buffer | run100–113 | 60 Hz nei tratti leggeri |
| Renderer su due thread (`rsx_mt_backend.c`) | run130–132 | il renderer non è più il limite |
| Regolatore a tentativi; attesa di fine frame nativa | run135–137 | 60 Hz nei tratti leggeri, 30 nei pesanti (14,6–19 ms per immagine) |
| Sette funzioni native di disegno e animazione | **run139** | **60 FPS per tutto il duello**, 12–16 ms per immagine |

**Dove siamo adesso (run139).** Tutto è legato al refresh a 60 Hz: il thread `render-draw` passa ~10,5 dei suoi ~13,8 ms per immagine ad aspettare un'immagine libera dello schermo (`acquire`), il thread FIFO aspetta ~2,6 ms la coda piena e il gioco aspetta la GPU 4–5 ms. Lavoro reale per immagine: gioco ~9–11 ms, FIFO ~8,8 ms, `render-draw` ~3,3 ms. Le immagini arrivano spesso appena in tempo (`late` alto nel log del regolatore) e la GPU ha picchi isolati di 20–24 ms: il margine per scene più pesanti va misurato.

**Ancora da fare**
1. Misura del margine con `diag.on` (tempo GPU `t31`, CPU per thread `t25`) nello stesso duello; da dove vengono i picchi GPU.
2. ~~Shader mancanti di Freezer~~: catturati e aggiunti (`dc0dfea`).
3. Bug degli scatti nelle animazioni (aperto dalle funzioni native del 31 ottobre–1 novembre, rimandato fino ai 60 FPS).
4. Mappa DU e schermata livello: non più misurate dall'inizio del lavoro sui 60 FPS.
5. ~~Audio che gracchia~~: risolto con il ritmo regolare dei blocchi (vedi registro, `audio-steady`).

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
| 1.1 | **Overclock come via per i 60 FPS: scartato.** Le misure si fanno al clock normale (CPU 1020 MHz) e ogni guadagno viene da codice più efficiente. Dal 2 novembre il menu debug offre un overclock facoltativo (CPU/GPU/RAM, solo valori consigliati, DEFAULT di base), che non entra nelle misure. | | |
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
| `opt5-prof` (run60–62 contro run58) | T41 `ppu_regcache.py`: registri PPU in variabili locali nel codice lifted | Duello | guest 25,8 ms/passata | 25,1 ms/passata | Scartata (spenta di default). Guadagno ~3%; personaggi che scattano (run60) e crash del renderer su un indirizzo vertex non valido emesso dal guest (run61) |
| `opt5-norc-prof` (run63) | Stessa build senza regcache | Duello | | guest ~25–26 ms, renderer ~16 ms per frame | Nessun crash né scatto: regcache era la causa |

Nota sui confronti: dalle prove seguenti ogni build si confronta solo con una run fatta nella stessa configurazione (stessi `diag.on`, profiler PPU e metriche). Le build di profilo servono per le proporzioni, non per gli FPS assoluti. La correzione di "regcache ~3%" sopra: run60 (senza profiler PPU) era stata confrontata con run58 (con profiler), quindi quel numero non è affidabile.

| Build (`-Feature`) | Modifica | Scenario | Prima | Dopo | Esito |
|---|---|---|---|---|---|
| `opt7-prof` (run65 contro run64, diag.on + profiler) | Accessor della memoria guest con controllo corto e `always_inline` | Duello | guest 27,3 ms/passata, codice 21,0 MB | 40,7 ms/passata, codice 38,2 MB | Scartata. +81% di codice, thread guest +49%: limite della cache istruzioni |
| `opt8-prof` (run66 contro run64) | File lifted compilati con `-Os` | Duello | CPU guest 29,2 ms/frame, codice 21,0 MB | 35,3 ms/frame, codice 15,5 MB | Scartata (`PORT_SWITCH_PPU_OS` OFF). Codice più piccolo ma ottimizzato peggio |
| `meas1-prof` (run67) | Misura: profiler ad albero (tempo inclusivo per funzione) e tempo GPU per frame (`t31-gpu-time.log`) | Duello | | GPU ~13 ms/frame (picchi 25–36 ms); "disegna una mesh" `0x0001AA00` = 29% del thread guest, applicazione materiale `0x0002C088` ~10% | Baseline per l'opzione A (riscrittura nativa del disegno mesh) |
| `wtrack2-prof` (run69) | Tracciamento delle scritture di `0x0002C088` | Duello, mappa DU, livello | | 2 byte del materiale, campi renderer +0x284..+0x2A0, puntatore GCM e ~135 byte di comandi per chiamata | Elenco completo degli effetti, base della verifica |
| `native1-prof` + `native-verify` (run70) | `0x0002C088` nativa con i 20 metodi del renderer e i 19 writer libgcm | Duello, mappa DU, livello | | 1.035.238 chiamate confrontate byte per byte, 0 differenze | Corretta |
| `native1-prof` (run71 contro run67, diag.on + profiler) | Stessa, modalità normale | Duello | CPU guest 30,9 ms/frame | 25,2 ms/frame | Tenuta. Parte del guadagno è costo del profiler evitato: guadagno reale da misurare con build leggere |
| `native1-metrics` (run72 nativa contro run73 con `native-material-off`; stessa build leggera con metriche, senza diag.on né profiler) | `0x0002C088` nativa | Duello | CPU guest 26,8 ms/frame, preparazione disegno 20,9 ms/passata | 21,8 ms/frame, 16,6 ms/passata | Tenuta: −5,0 ms/frame reali. FPS invariati (~30): il thread guest è ancora sopra 16,7 ms |
| `wtrack3-prof`, `wtrack4-prof` (run74, run76) | Tracciamento delle scritture delle funzioni di stato del draw (shader, texture, array di vertici, parametri) | Duello, mappa DU, livello | | Solo campi del renderer, blocco parametri sullo stack del chiamante, puntatore GCM e comandi; conteggi atomici degli shader non visti dal tracciatore | Base della verifica per 2a e 2b |
| `native3-metrics` + `native-verify` (run77) | Native: bind vertex/fragment program (con i parametri di default semplici), `0x0002BAE8`, `0x00053A7C`, `0x0004E288`, `0x0004EB98`, `0x00050844` | Duello, mappa DU, livello | | Oltre 9 milioni di chiamate confrontate byte per byte, 0 differenze | Corretta |
| `native3-metrics` (run78 tutto nativo contro run79 con `native-shader-off` + `native-draw-off`; build leggera con metriche) | Stesse funzioni, modalità normale | Duello | preparazione disegno 18,3 ms/passata, somma task 21,2 ms | 16,9 ms/passata, somma task 19,6 ms | Tenuta: circa −1,5 ms per passata. La CPU per frame (24,5 → 19,8 ms) include anche differenze di scena tra le due sessioni (la logica, non toccata, varia da 2,9 a 1,2 ms) |
| `native4-metrics` (run83 veloci contro run84 con `native-mesh-off`; build leggera con metriche) | Copie veloci generate (`scripts/ppu_mesh_fast.py`) dei corpi mesh `0x0001AA00` e `0x000328A8`: registri in variabili locali, salvati e ricaricati tutti attorno a ogni chiamata | Duello | task `0x000D2310` 4,6 ms/passata | 4,7 ms/passata | Nessun guadagno |
| `mesh2-verify` + `native5-metrics` (run85; run86 contro run84) | Stesse copie, ma attorno alle chiamate solo argomenti e valori di ritorno (regole di chiamata PS3) | Duello | task `0x000D2310` 4,6 ms/passata | 569.000 chiamate verificate, 0 differenze; 5,3 ms/passata (sessione corta) | Nessun guadagno. Restano spente con `native-mesh-off` |
| `mesh-prof` (run87, profiler PPU, `native-mesh-off`) | Misura: il campionatore registra anche il punto di chiamata dentro `0x000328A8` (`site=` in `t23-ppu-hot.log`) | Duello | | `0x000328A8` 7,7% del thread principale (~2,5 ms/frame), di cui 4,0% codice proprio (~1,3 ms) | Riscrittura a mano scartata: al massimo 1–1,5 ms. Il 21,9% di run80 veniva da una build con il tracker delle scritture |

**Budget del duello dopo run86/run87** (build leggera; il 27% "proprio" del ciclo principale `0x00159BE8` è il sonno del regolatore dei frame, non un'attesa di GPU o SPU): thread del gioco ~21–22 ms di CPU per immagine a 30 Hz (2 passi logici), cioè ~18 ms per immagine a 60 Hz; renderer ~16–17 ms per frame; GPU 3–8 ms. Mancano ~4–5 ms sul thread del gioco e ~3–4 ms sul renderer.

| Build (`-Feature`) | Modifica | Scenario | Prima | Dopo | Esito |
|---|---|---|---|---|---|
| `pad-prof` (contro run87) | `t21-input.log` aperto solo con `diag.on`: ogni pressione di tasto scriveva una riga con `fflush` sulla SD dal thread del gioco (10–35 righe/s in duello), anche nelle build leggere | Duello | `cellPadGetData` + `cellPadGetInfo2` 4,8% del thread principale (~1,5 ms/frame); lavoro per immagine del regolatore (tempo reale) 27,3 ms | run88: sotto la soglia del profilo (~0%); lavoro per immagine 25,8 ms | Tenuta: −1,5 ms di tempo reale per frame. La CPU per frame non cambia (21,8 → 23,4 ms, sessione con più carico di disegno): il tempo perso era attesa della scrittura su SD, non calcolo |
| `math1-prof` + `native-verify` (run89) | Native `0x002A733C` (nucleo sinf/cosf della libm del gioco) e `0x000252B0` (matrice da quaternione e traslazione, moltiplicata per la matrice globale `0x0047A300`, AltiVec convertito riga per riga in NEON) | Duello, mappa DU, livello | | 6.297.114 + 855.766 chiamate confrontate, 0 differenze, nessun ricorso al codice tradotto | Corrette |
| `math1-prof` (run90 contro run88, stesso profilo, `native-mesh-off`) | Stesse, modalità normale | Duello | lavoro per immagine 25,8 ms, CPU guest 23,4 ms/frame, preparazione disegno 17,2 ms/passata | 23,6 ms, 21,7 ms/frame, 15,9 ms/passata | Tenuta: circa −2 ms per frame |
| `math2-prof` + `native-verify` (run91) | Native `0x0001F1E8` (punto per la matrice globale), chiamata diretta di `cellGcmAddressToOffset` (stub `0x002B6E00`, senza il dispatcher HLE generico), cache della traduzione dell'anello nel percorso delle costanti `0x001FCAC4` | Duello, mappa DU, livello | | 4.707.216 + 748.769 chiamate confrontate (più 7,3 M sin/cos e 0,9 M matrice), 0 differenze; costanti: 4,9 M chiamate veloci, 1.269 giri dell'anello senza attese | Corrette |
| `math2-prof` (run92 contro run90, stesso profilo, `native-mesh-off`) | Stesse, modalità normale | Duello | lavoro per immagine 24,5 ms (finestre di duello), costanti 0,431 µs/chiamata | 23,3 ms, 0,367 µs/chiamata; `0x0001F1E8` e `0x002B6E00` spariscono dal profilo | Tenuta: circa −1,2 ms per frame. Scena un po' diversa (manca il task `0x00107458`) |
| `math2-metrics`, build leggera (run93 contro run94, stessa build, stesso duello) | Sin/cos, matrice, punto, `cellGcmAddressToOffset` native (run93) contro le stesse spente con `native-sincos-off`, `-matrix-off`, `-point-off`, `-a2o-off` (run94); `native-mesh-off` in entrambe | Duello | lavoro per immagine 23,1 ms, a 30 Hz il 95% del tempo | 21,2 ms (tratti a 18,3–18,9 ms), a 30 Hz il 94% | Tenuta: circa −2 ms per frame nella build leggera. Ancora a 30 Hz: serve lavoro < 14 ms per passare a 60 |
| `math3-metrics` + `native-verify` (run95–97) | Native le piccole funzioni chiamate spesso: `0x0001EEF8` (rotazione della matrice globale × vettore), `0x0001F164` (scala delle righe), `0x0001EF6C` (traslazione), `0x000297B8` (somma vec3), `0x000299AC` (lunghezza vec3), `0x000357A0` (sfera contro 6 piani), `0x00024F58` (rotazione da 3 angoli, 6 sin/cos), `0x002993A0` (`memset`, solo RAM normale senza righe prenotate dalle SPU) | Duello | | run97: tutte 0 differenze (da 0,9 a 13,4 M chiamate ciascuna; `memset` 1,8 M). In run95 la `memset` non partiva mai: rinunciava appena il controllo delle prenotazioni SPU era acceso (sempre, in questo gioco); ora controlla solo le righe che scrive | Corrette |
| `math3-metrics`, build leggera (run98 contro run99, stessa build, stesso duello) | Le 8 funzioni attive (run98) contro spente con `native-rotvec-off`, `-scale-off`, `-translate-off`, `-add3-off`, `-len3-off`, `-planes-off`, `-euler-off`, `-memset-off` (run99) | Duello | lavoro per immagine ~21 ms (21,5 con la chiusura del duello), task `0x000D2310` ~5,2 ms | 18,0 ms (tratti a 15,3–15,9), task ~4,65 ms | Tenuta: circa −3 ms per frame, il doppio della stima dal profilo. Il passo di logica in più a 30 Hz costa ~2,2 ms, quindi un'immagine a 60 Hz costerebbe ~15,8 ms |
| `pace2-metrics` (run100) | Regolatore: decide sul costo stimato di un'immagine a 60 Hz (lavoro meno i passi di logica in più), sale sotto 15,5 ms, scende sopra 16,7 ms, poi 120 immagini di attesa. Prima confrontava il lavoro a 30 Hz (2 passi) con 14 ms, cioè chiedeva ~10,7 ms a 60 Hz | Duello | 30 Hz quasi sempre | Stima 15,6 ms in media; 60 Hz solo il 9% del tempo, 42 cambi 30/60 (uno ogni ~4 s): a 60 Hz molti frame in ritardo | Al limite: servono ~2 ms di margine. Soglia di salita portata a 14,5 ms per non fare avanti e indietro |
| `gcm1-metrics` + `native-verify` (run102) | Native gli scrittori della lista dei comandi `0x0004A39C`, `0x0004A428`, `0x0004A300` e le funzioni con chiamate virtuali `0x0004A118`, `0x0004E100` (trasposta 4×4), `0x0004EE80` (con `cellGcmAddressToOffset`). La `memcpy` `0x00292628` era già nativa (`dbz3_ppu_native.cpp`) | Duello | | Scrittori: 4,0–4,2 M chiamate ciascuno, 0 differenze; le tre con chiamate non si possono eseguire due volte (solo native), duello senza errori | Corrette |
| `gcm1-metrics` (run103 contro run100; sessione B non fatta) | Le 6 funzioni sopra | Duello | stima di un'immagine a 60 Hz 15,6 ms, 60 Hz il 9% del tempo | 14,5 ms, 60 Hz il 38% | Circa −1,1 ms. A 60 Hz però 144 frame su 150 arrivano in ritardo: il flip `0x000487FC` dura ~8 ms (0,2 ms a 30 Hz) perché aspetta il renderer, che per immagine fa 13,6 ms di FIFO + 4,8 ms di presentazione (idle 2,7 ms). Il collo di bottiglia a 60 Hz ora è il renderer |
| `gcm1-metrics` + `diag.on` (run104) | Misura: GPU per frame (t31), CPU dei thread (t25), presentazione del renderer | Duello | | GPU ~13,1 ms per frame (picchi ~27); renderer a 60 Hz: FIFO 12,5 ms (CPU 12,3), presentazione 5,6 ms ma solo 0,13 ms di CPU; attesa dei fence 0 | La presentazione aspetta lo schermo: con 2 immagini e la sincronizzazione verticale, un frame in ritardo perde un intero refresh |
| `tribuf-metrics` (run105 contro run103) | Triple buffering: `DK_NUM_SWAP` da 2 a 3 (`rsx_deko3d_backend.c`, overlay) | Duello | tentando i 60: 47,3 immagini/s, presentazione 4,8 ms, 144 frame su 150 in ritardo | 49,5 immagini/s, presentazione 3,6 ms, 79 su 150; alcuni secondi a 55–60 | Tenuta, guadagno piccolo. Ora il limite è la CPU del renderer: FIFO ~13,6 ms per immagine nel duello (~370 disegni): lettura dei vertici ~3,3 ms, disegno nel backend ~2,5, texture ~2,0, bind ~1,0, pipeline ~0,85, hash ~0,65, costanti ~0,45 |
| `fetchsplit-metrics` (run106) | Misura: tempi dei pezzi della lettura vertici (`t23-draw-prof.log`, riga `fetch_split`) | Duello | lettura vertici 2,67 ms per immagine | chiave della cache (hash) **1,71 ms**, preparazione 0,12, conversione 0,10 (solo ~516 vertici convertiti per immagine su ~22.000), indici 0,03, doppioni 0,04, salvataggio 0,01 | La conversione non pesa: pesa l'hash XXH64 di ~1 MB per immagine (~0,6 GB/s, moltiplicazioni a 64 bit lente sull'A57). Anche l'hash delle texture (0,77 ms per ~0,8 MB) usa XXH64 |
| `crchash-metrics` (run107 contro run106) | Hash con le istruzioni CRC32/CRC32C dell'A57 (ogni parola in due catene con polinomi diversi, 4 catene, mescolate a 64 bit) per la chiave della cache dei vertici e per il controllo delle texture (`rsx_texture_hash.h`, `rsx_draw_engine.c`) | Duello | hash texture 0,77 ms, chiave vertici 1,71 ms | 0,39 ms, 1,55 ms | Tenuta, ma la chiave dei vertici quasi non scende: il costo non era il calcolo dell'hash |
| `keysplit-metrics` (run108) | Misura: chiave della cache divisa in 4 parti | Duello | chiave 1,48 ms | descrizione 0,08, indici 0,54 (93 KB, ~47.000 indici letti byte per byte), formati 0,15, dati dei vertici 0,70 (960 KB a ~1,3 GB/s: limite della memoria); ricerca texture 0,64 ms (~368 voci scorse per chiamata) | |
| `rend2-metrics` (run109 contro run108) | Range degli indici con NEON (8 indici alla volta; se la base può superare i 20 bit, ciclo esatto di prima); scorciatoia nella ricerca delle texture (tabella di 2048 suggerimenti, verificati campo per campo) | Duello | indici 0,54 ms, ricerca texture 0,64 ms, sezioni del renderer 8,97 ms per immagine (328 disegni) | 0,26 ms, 0,08 ms (5,5 voci per chiamata), 8,18 ms (354 disegni); FIFO del renderer a 60 Hz 13,2 → 12,7 ms | Tenuta: circa −0,8 ms per immagine nel renderer |
| `rend2-metrics` + `diag.on` (run110) | Misura GPU con triple buffering (scala 1, FXAA spento) | Duello | | GPU ~11,5 ms per frame nei tratti a 60 Hz (~60% occupata), 12–15 ms a 30 Hz, picchi 25–36 ms; renderer a 60 Hz: FIFO 12,4 + presentazione 3,5 + sonno 3,3 ms | La GPU in media non è il limite |
| `present-metrics` (run111) | Sonno del renderer 0,25 ms invece di 1 ms con la FIFO in movimento (`main.cpp`); log `t38-present.log` | Duello | sonno 2,8 ms per immagine, attesa al flip 5,9 ms | sonno 3,1 ms (invariato: il renderer è davvero senza comandi), attesa al flip 4,0 ms; presentazione: richiesta immagine ~0,2, preparazione e invio 1,0–3,7, consegna ~0,25 ms | Il sonno breve non serve; il tempo è nella preparazione e invio |
| `present2-metrics` (run112) | Misura: preparazione e invio divisa | Duello | | registrazione ~0,05 ms, invio ~0,06 ms, **attesa del fence 0,8–6,6 ms** (picchi 30–50 ms) | Con 2 gruppi di comandi ogni picco della GPU ricade subito sul renderer |
| `sets3-metrics` (run113 contro run112) | 3 gruppi di comandi in volo (`DK_SETS 3`, +40 MB): il renderer aspetta il frame N-2 invece di N-1; texture liberate raccolte un frame più tardi | Duello | attesa del fence 0,8–6,6 ms, presentazione 3,2 ms | attesa del fence 0–0,7 ms, presentazione 1,7 ms; renderer a 57,5 immagini/s nei tratti a 60 Hz; 17 secondi a 60 FPS pieni | Tenuta. Ora il limite nelle scene pesanti è il thread del gioco (~16 ms stimati a 60 Hz) |
| `pace155-metrics` (run114) | Soglia di salita del regolatore di nuovo a 15,5 ms | Duello pesante | | 60 Hz il 7% del tempo: stima 16,5–20 ms per quasi tutto il duello; solo 8 cambi 30/60 | La soglia non fa danni; il duello pesante è limitato dal thread del gioco |
| `heavy-prof` (run115) | Profilo del thread del gioco, tratti pesanti contro leggeri | Duello pesante | | `0x002909C0` 13,3% del thread nei tratti pesanti (0,7% nei leggeri): ciclo di attesa (sys_timer_usleep 30 µs) su un flag scritto dopo i task SPURS avviati da `0x00290948` (`_cellSpursSendSignal`) | Il gioco aspetta, non calcola |
| `spu-prof2` + `diag.on` (run116–117) | Profilo SPU (corretto: le funzioni fuse da `spu_fuse.py` hanno un solo marcatore con ripristino, prima del dispatch; i membri usano `SPU_PROFILE_STAMP`; run116 andava in crash) | Duello pesante | | I task SPURS sono quasi sempre fermi; **`spu:bnusCore` (audio) usa ~99% di un core sempre**, altri thread audio ~25%; core occupati ~60/77/75%. 7 funzioni fanno il 72% di `bnusCore` (`0x0AD98` 19%, `0x0A9A0` 14%, `0x08748` 12,5%, `0x0F98C` 8%, `0x0AD30` 6,5%, `0x0B9A8` 6%, `0x08A10` 5%); 12–16% dei blocchi audio non pronti in tempo | **Non valido**: nella build profilata la strumentazione partiva dal codice SPU originale e perdeva la patch del riverbero (`CMakeLists.txt`, corretto dopo run119). Il 99% e le 7 funzioni sono il riverbero acceso |
| `audiolow-metrics` (run118 contro run114) | `bnusCore` alla priorità host più bassa (`PORT_SWITCH_AUDIO_SPU_LOWPRIO`, ora ON di default) | Duello pesante | 60 Hz il 7% del tempo, stima a 60 Hz 17,1 ms, blocchi audio mancati 8,8–15,9% | **60 Hz il 79% del tempo**, stima 13,1 ms, 58,9 immagini/s nei tratti a 60 Hz, blocchi audio mancati 11,9% | Da confermare: con il riverbero davvero saltato `bnusCore` usa solo ~12–27% di un core (run119), quindi gran parte della differenza è probabilmente la scena (run118 è durata ~5 minuti). Confronto A/B con lo stesso duello tramite gli interruttori `bnus-prio-normal` / `bnus-prio-high` |
| `spu-prof3` + `diag.on` (run119) | Profilo SPU con la patch del riverbero applicata (strumentazione sulla copia già corretta) | Duello pesante | | `bnusCore` 12–27% di un core; 85% dei suoi campioni in `0x11EA8` (scrive la mailbox e si ferma con stop 0x110 in attesa del PPU); blocchi audio mancati ~10%. In run118 il gioco scrive ~162 blocchi/s contro 187,5 consumati, con ~6 blocchi già pronti nel buffer: i mancati sono costanti (~25–30/s), non legati alla CPU | L'audio non è limitato dalla CPU ma dai tempi: da verificare se si perdono notifiche per blocco (contatore `notify_fail` in `t23-audio.log`) |
| `bnusprio2-metrics` A/B (run120 contro run121, stesso duello, `diag.on`) | Priorità di `bnusCore` dalla SD: minima (default) contro alta (`bnus-prio-high`) | Duello pesante | minima: 60 Hz il 26%, stima 16,7 ms, blocchi audio mancati 9,6% | alta: 11%, 17,7 ms, 8,8% | La minima resta: ~1 ms in meno sul gioco, audio uguale. Notifiche cellAudio: 0 perse (`notify_fail=0`) |
| `audiozero-metrics` (run122) | `audio-sentinel-off`: blocchi suonati azzerati come sul firmware invece del valore di misura T29 | Duello | | Audio uguale a orecchio | Non è la causa del gracchiare (resta spento) |
| `postfx-probe`…`probe4` (run123–129) | Misura dell'attesa di fine frame `0x002909C0` (`dbz3_postfx_probe.cpp`, `t39-postfx.log`) | Duello pesante | | Il flag lo alza il callback del comando utente (`cellGcmSetUserHandler` → `0x00049884` → segnale ai 3 task del filtro, saltato, che scrive subito `0x0047C634`). Nei tratti pesanti il gioco aspetta 0,6–1,3 s ogni 5 s perché il renderer non è ancora arrivato al comando utente del frame precedente | È un vero punto di sincronizzazione con l'avanzamento della GPU |
| `postfx-pump` (run126) | Il ciclo d'attesa di `0x002909C0` consegna i callback GCM in sospeso a ogni giro (`ppu_gcm_pump`); prima arrivavano solo quando un thread chiamava una funzione di libreria emulata | Duello pesante | | Attese ancora presenti nei tratti pesanti (il callback non è ancora maturato) | Tenuta (sicura), guadagno non misurabile da sola. `postfx-pump-off` per tornare indietro |
| `usercmd-early2` (run130) | Callback del comando utente anticipato appena il comando è nella FIFO (scansione in anticipo in `cellGcmSys.c`) | Duello pesante | | **Il renderer si blocca**: il callback dice al gioco che la GPU ha superato quel punto e il gioco riusa la memoria del frame mentre il renderer la legge ancora | **Scartata**: spenta di default (`usercmd-early-on` solo per esperimenti). Per togliere l'attesa serve un renderer più veloce |
| `bemt-step1` (run130) | Backend di passaggio `rsx_mt_backend.c`: le chiamate del motore al backend deko3d registrate in una coda (con copia dei dati) ed eseguite alla presentazione o prima di una chiamata che vuole una risposta, sullo stesso thread | Duello | | Grafica normale. Per frame: lavoro deko3d ~4,3 ms, ~4,7 MB copiati, ~15–20 chiamate sincrone (soprattutto `surface_view` ~12–16 e `surface_blit` ~4) | Registrazione corretta |
| `bemt-step2` (run131) | Secondo thread `render-draw` che esegue la coda (massimo 2 frame in attesa); `surface_view` con maniglie virtuali risolte dal secondo thread; `surface_blit` controllato e accodato | Duello pesante | | Grafica normale; il renderer aspetta il secondo thread solo pochi ms ogni 5 s; il secondo thread fa ~3,7 ms per immagine; presentazione sul renderer 3,8 → 0,5 ms. Ma registrare costa quasi quanto eseguire (bind 0,89 → 2,01 ms) | Funziona, guadagno ancora nullo |
| `bemt-step2b` (run132) | Vertici dalla cache passati per puntatore (voci della cache liberate 8 presentazioni dopo); costanti in differenza; spinlock | Duello pesante | | Registrazione dei disegni 1,07 → 0,21 ms; dati in coda ~3,5 MB/frame; le costanti in differenza non servono (cambia più di metà del blocco a ogni bind) e sono state tolte. **Il renderer aspetta il gioco ~5,6 ms per immagine** | Il renderer ha margine: ora il limite è il thread del gioco (~16,5 ms stimati a 60 Hz nel duello pesante) |
| `paceprobe` (run135) | Regolatore a tentativi: a 30 Hz, con la stima sotto 20 ms, prova 60 Hz per 60 immagini e lo tiene se il costo misurato resta entro 16,7 ms; un tentativo fallito raddoppia l'attesa prima del successivo (2–30 s) | Duello (scena non annotata) | | Menu a 60 Hz; nel duello i tentativi misurano 14–17 ms con molti frame in ritardo e quasi tutti falliscono; niente avanti e indietro continuo | Tenuto (commit `f9a317f`). Da run136 la scena di prova fissa è **Goku contro Vegeta** |
| `gameprof-gv` (run136) | Profilo del thread del gioco (`-ProfilePpuFunctions`, `diag.on`), Goku contro Vegeta | Duello | | Pacer a 30 Hz il 62% del tempo; mesh `0x0001AA00` 18% incl., ma `0x000328A8` passa il tempo in funzioni già native (materiali 1,4%, stato di disegno 1,5%, parametri 0,9%; il lift resta ~2%); attesa `0x002909C0` 7%; resto piatto (1–2% per funzione). Task: `0x000D2310` 18%, `0x00094DD8` 9,4%, `0x000700D4` 8,2%, `0x0006FDF8` 6,5%, `0x000D1490` 6,5%, `0x00061CFC` 5% | Riscrivere le mesh a mano non conviene |
| `fwait-metrics` (run137) | Attesa di fine frame `0x002909C0` nativa: consegna i callback GCM in sospeso a ogni giro (come `postfx-pump` di run126, ora con il renderer su due thread) e misura l'attesa divisa in prima/dopo che la FIFO raggiunge il comando utente (riga `fwait` in `t26`) | Goku contro Vegeta | | Consegna ~0 ms; attesa nel duello 0,1–1 ms per immagine, tutta "FIFO non ancora arrivata"; duello a 60 Hz 14,6–17 ms, nei tratti pesanti 17–19 ms | Tenuta, attiva di default (`usercmd-pump-off` per tornare al ciclo tradotto). L'attesa della GPU non è più un limite: il limite è il lavoro del thread del gioco |
| `b2-menu-save` (run138–139) | Funzioni native lotto 2 in `dbz3_native_math.cpp`: colore materiale `0x0002C7D8`, texture `0x00031DC8`, preparazione disegno `0x0002F56C`, quad `0x00030790`, sprite `0x00030DF0`/`0x00031024`, animazione ossa `0x000207C0` (elemento 0 dei vettori in scalare, stesso ordine delle operazioni). Interruttori `native-*-off`, `native-b2-off` per tutte | Goku contro Vegeta | run137: 150–190 immagini ogni 5 s, ritorni a 30 Hz | **~300 immagini ogni 5 s (60 FPS) in tutto il duello**, stima 12–16 ms, nessun ritorno a 30 Hz; molte immagini appena in ritardo; il gioco aspetta la FIFO 4–5 ms per immagine e il thread `render-draw` è occupato ~16 ms per immagine | Tenuta, fluido a vista. **Il limite ora è il renderer** |
| `shadercap` (build) | `ShaderCapture = $true` nella configurazione di default, per catturare gli shader di Freezer. Corretta la build leggera, che non compilava da `a873547` (`cellGcmSys.c` usava `armGetSystemTick` senza `switch.h`; ora legge `cntpct_el0` direttamente) | | | Compila | In attesa della cattura |
| `audio-steady` | Blocchi audio mescolati ogni 5,33 ms sull'orologio di sistema invece che ai rilasci dei buffer audout (che arrivano a scatti contro il ciclo di ~5 ms della Switch); 4 blocchi in coda all'avvio; contatori `bursts`/`resync` in `t23-audio.log`; `audio-steady-off` per tornare indietro | Duello | run139: porta 0 (effetti, voci) ~20% di blocchi suonati senza essere riscritti, porta 1 (musica, libmixer) ~1% e CRI 185,6 blocchi/s su 187,5 | Niente più gracchio a orecchio, in musica, effetti e voci | Tenuta. La causa: con il cursore che avanzava di due blocchi di colpo, i produttori del gioco scrivevano due volte lo stesso blocco e ne saltavano uno |
| Default (build `light`) | Build leggera di default; tutto il meglio senza file sulla SD: disegno mesh nativo non più compilato (`PORT_DBZ3_NATIVE_MESH` OFF, nessun guadagno in run83–86); renderer su due thread, audio a priorità minima e funzioni native attivi di default | | | Interruttori rimasti solo per prove: `render-mt-off`, `render-mt-sync`, `bnus-prio-normal`/`-high`, `native-*-off`, `audio-sentinel-off`, `usercmd-pump-off`, `native-b2-off`, `savedata-sd`, `audio-steady-off` (`usercmd-early-on` è stato tolto) | |
