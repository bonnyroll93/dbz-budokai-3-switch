# Revisione delle catture Switch: bug visivi e piano di indagine

## Materiale disponibile (28 ottobre 2026)

I tre file originali trovati in `C:\Users\bonny\Desktop\captures` sono stati copiati senza conversione in `userdata/screenshoot/switch-captures-20261027/`:

| File | Contenuto | SHA-256 |
| --- | --- | --- |
| `shot_1793127198_001.jpg` | Schermata iniziale, 1280×720 | `5A140A93A39B7562B0CBA556C29668C88992F6F28CA9A05063D06ED4D6E00CE0` |
| `clip_1793127263_002.mjpg` | Mappa Namek, transizione e combattimento, 139 fotogrammi a 640×360 | `AF81DD7D0BCEDBBD2149FC76B20305E46F56ADDD638BDB8AE1118A00BA771EAE` |
| `clip_1793127263_002.txt` | Tempi dei fotogrammi, fino a 29,838 s | `0C2F365D517D1B347D4E2F12DAF9EC51963EFA0F25A590F32A9E91DC17E933E1` |

In seguito, con DBI MTP attivo, sono stati copiati **23 JPEG nuovi** da `1: SD Card/switch/dbz-budokai-3-switch/captures/` in `userdata/screenshoot/switch-captures-20261028/`. Tutti sono 1280×720 e leggibili come JPEG. La cartella contiene `SHA256SUMS.txt` con un hash per file. I tre file precedenti erano già presenti sulla SD e non sono stati duplicati. L'Album DBI non conteneva catture di questo gioco.

Il `t19-stderr.log` corrente della SD riporta `[build] compiled Oct 27 2026 21:34:45`. Il log è stato copiato in `.tmp/visual-review/` per la lettura; il marker identifica la build attualmente presente sulla SD, ma non prova da solo che tutte le immagini, salvate in momenti diversi, provengano da quella build.

I fotogrammi estratti per la sola ispezione sono in `.tmp/visual-review/`. `userdata/` e le immagini sono escluse da Git; il documento è tracciabile, le catture restano locali.

## Bug osservati

| ID | Priorità | Evidenza | Difetto visibile | Stato e prima verifica |
| --- | --- | --- | --- | --- |
| V01 | Alta | Clip, frame 0, 25 e 50 (0–10,7 s); JPEG `1793151976`, `1793169809`, `1793169836`; `run39/map-pass-dump/pass008_t0.png` | Nella mappa di Dragon Universe le braccia e le mani di Goku appaiono bianche, su più aree. Nella clip, nel combattimento intorno al frame 125 (27,1 s), la pelle è colorata. | Aperto. Identificare i draw del corpo della mappa e confrontare shader, texture, colori dei vertici, costanti e stato con il corpo in combattimento. |
| V02 | Alta | JPEG `1793151976`, `1793151987`, `1793169809`, `1793169918`; `run39/map-pass-dump/pass008_t0.png` | Scarpate e coste mostrano bande nere alternate a segmenti di texture; evidente su prato, lago, neve e dietro il dialogo di salvataggio. | Aperto. Isolare il draw del bordo nei passaggi della mappa e confrontare texture, UV, wrap, mip, alpha e profondità con la resa di riferimento. La ricorrenza su più aree suggerisce un materiale o percorso di rendering condiviso, non una singola texture danneggiata. |
| V03 | Media | Clip, circa frame 0–60 (0–13 s) | L'acqua e le superfici sulla mappa Namek formano grandi campiture verde/azzurre poco leggibili; nelle viste distanti si vede una separazione netta tra regioni. | Da confermare con una cattura a risoluzione piena e un riferimento della stessa scena. Il colore del cielo e dell'acqua di Namek può essere intenzionale; non classificarli come bug solo perché saturi. Verificare se mancano trasparenza, rifrazione o texture di dettaglio. |
| V04 | Alta, da confrontare | JPEG `1793171104` e `1793171140`–`1793171151`; `docs/GRAPHICS_INVESTIGATION.md` | Il pubblico del Tenkaichi ha una trama molto fitta e irregolare; nella fascia pubblicitaria superiore compaiono rettangoli neri. Aura e proiettili sono presenti, ma le immagini da sole non dimostrano che il loro colore o blend sia errato. | Confrontare la stessa arena e mossa con RPCS3/PS3. Per le fasce nere verificare texture atlas e alpha del draw; per folla ed effetti identificare texture, UV, sampler e blend prima di attribuire la causa. |
| V05 | Alta, già segnalato | `docs/GRAPHICS_INVESTIGATION.md` | Materiali che diventano neri o artefatti durante il combattimento. | Segnalazione preesistente, non visibile nella clip importata. Riprodurre prima e dopo il cambio nello stesso combattimento per verificare cache e durata delle risorse texture. |
| V06 | Media, già segnalato | `docs/DRAGON_UNIVERSE_RUN07.md` | Deformazione orizzontale del personaggio nell'acqua. | Segnalazione da foto precedenti; non individuata con certezza nella nuova clip Namek. Catturare i passaggi prima e dopo la rifrazione. |
| V07 | Alta | JPEG `1793169836` (prato), `1793152017` (ponte) | Tasselli quadrati regolari con texture di scarpata e bordo azzurro affiorano attraverso il terreno; i pannelli del ponte mostrano pattern rosso/blu ripetuto e apparentemente fuori posto. | Aperto. Verificare se sono draw/mesh spurie o campionamenti da un atlas errato. Confrontare geometria, UV, indirizzo/byte texture e ordine dei passaggi con la scena di riferimento. Tenere separati terreno e ponte finché non risulta che condividono il materiale. |
| V08 | Critica | JPEG `1793169655`, `1793169735`, `1793169879`, `1793169908`, `1793169918`, `1793171060`, `1793171140`, `1793171188` | Testo di schede, dialoghi, menu, opzioni di salvataggio e nome mossa appare spezzato, con lettere mancanti o sostituite da blocchi di texture. Alcuni testi restano leggibili nella stessa schermata. | Aperto e ripetibile in più interfacce. Isolare un draw di testo guasto e uno sano nello stesso frame; confrontare atlas, coordinate UV, rettangolo sorgente, upload e invalidazione texture, alpha/blend e ordine dei draw. Priorità alta perché rende il gioco difficile da usare. |

La schermata iniziale importata non mostra un artefatto evidente. I fotogrammi scuri della clip coincidono con la transizione verso il combattimento; non sono classificati come difetto. Non usare i file `run39/map-pass-dump` come prova della clip Namek: la cattura `run39` parte dal frame 2011, 45 secondi dall'avvio, e raffigura un'altra mappa. I colori rosa/rossi dell'aura nel duello potrebbero appartenere alla tecnica mostrata: serve il riferimento prima di registrarli come bug.

## Piano operativo

1. **Fissare build e riferimento.** Per le catture importate leggere `[build] compiled` nel log Switch della sessione, poi riprodurre la stessa scena in RPCS3/PS3. Raccogliere un frame sano e uno guasto quando il materiale cambia nel tempo. Conservarne nome, hash e stato del gioco.
2. **Isolare per primo V08, poi V01/V02/V07.** Il testo guasto è riproducibile in molti menu: catturare un frame con testo sano e guasto contemporaneamente. Per la mappa usare `map-capture-after` o `RSX_ENGINE_PASS_DUMP` con `RSX_ENGINE_PASS_DUMP_DRAWS=1`, includendo braccia bianche, bordo a bande e tasselli nel terreno. I dump esistenti di `run39` individuano il passaggio della mappa (007/008), ma `draws.txt` registra soltanto pipeline e texture: manca ancora l'associazione certa tra pixel difettoso e draw.
3. **Trovare il primo stadio che diverge.** Per il draw incriminato salvare VS/PS, texture e byte, formato e mip, UV, wrap, remap, colori/normali dei vertici, costanti, depth, alpha test, blend e target prima/dopo. Confrontare il risultato con RPCS3 o PS3 nella stessa scena. Se la texture è corretta ma il colore finale è bianco, controllare prima shader, attributi e costanti; se i byte o l'indirizzo cambiano nel tempo, controllare invalidazione della cache e riuso della memoria guest.
4. **Indagare V03 e V06 per passaggio.** Confrontare il target prima e dopo lo shader di distorsione e verificare l'ingresso depth. `ps_43b4c9959faacec2` è un candidato di rifrazione; `be_depth_snapshot()` nel backend deko3d è un limite noto, ma la causalità va provata sul draw interessato.
5. **Isolare V04 e V05 in scene dedicate.** Per pubblico ed effetti catturare un frame per materiale e identificare texture, sampler e alpha. Per il nero che compare nel tempo registrare lo stesso draw prima/dopo, con chiave della cache, generazione della texture e numero di mip. Il cambio recente alla chiave della cache deve essere verificato sulla build effettivamente installata, senza attribuirgli già una correzione.
6. **Correggere un difetto per volta e ricontrollare.** Salvare screenshot prima/dopo della stessa build e posizione, verificare il draw interessato e controllare che gli altri materiali della scena non regrediscano. Misurare gli FPS separatamente: i readback dei dump rallentano la GPU.

Riferimenti tecnici: `docs/GRAPHICS_INVESTIGATION.md`, `docs/DRAGON_UNIVERSE_RUN07.md`, `third_party/ps3recomp/libs/video/rsx_draw_engine.c`, `patches/ps3recomp-overlay/libs/video/rsx_deko3d_backend.c` e `ports/dbz3/switch_app.c`.
