# Indagine grafica: texture, tiling e alpha

## Ambito

Il gioco è giocabile; la priorità è correggere pubblico dell'arena Tenkaichi, aura, proiettili energetici e gli altri materiali con shader, ripetizione o trasparenza errati. Le osservazioni visive dell'utente sono considerate valide.

Osservazione successiva dell'utente: durante un combattimento alcuni shader inizialmente corretti diventano improvvisamente neri o presentano artefatti. Questo rende prioritaria la verifica di riuso degli indirizzi guest, cache delle texture e durata delle risorse GPU.

## Evidenza disponibile

- In `build/logs/dbz3/dev/run15/t19-stderr.log`, le 32 nuove pipeline riportate dal draw engine sono `ok`. I riepiloghi deko3d fino a 18.000 present riportano `tex_fail=0`. Questo esclude un fallimento generale di compilazione o allocazione in quella prova, ma il log limita a 32 le righe per singola pipeline e non identifica i draw difettosi.
- `t22-gpu.log` è accodato fra le prove: contiene vecchi `pipeline refused` relativi a shader aggiunti successivamente. Non attribuire quelle righe alla build `run15` senza una marcatura temporale.
- `run15/t28-tex/index.txt` cattura 192 texture DXT, quasi tutte BC2, con remap `0xAAE4`. S/T sono `3,3` in 180 casi e `1,1` in 12: rispettivamente clamp e repeat. La cattura è selettiva e non associa queste texture al pubblico o alle particelle.
- Il backend deko3d traduce correttamente `0xAAE4` in RGBA identità. La conversione texture BC2 e il blend sono implementati, ma una pipeline valida può comunque produrre coordinate o alpha errati.

## Difetti concreti del renderer da correggere separatamente

- `dk_wrap()` in `patches/ps3recomp-overlay/libs/video/rsx_deko3d_backend.c` ora traduce i modi RSX 5–8 nei rispettivi modi clamp e mirror clamp di deko3d. Prima li appiattiva in clamp to edge. È un fix di correttezza del sampler; non spiega da solo le texture già catturate, che usano soltanto i modi S/T 1 e 3.
- La chiave della cache in `third_party/ps3recomp/libs/video/rsx_draw_engine.c:eng_texture_slot()` non comprendeva il numero di mipmap. Due binding dello stesso indirizzo e formato con catene mip diverse potevano quindi riutilizzare la stessa immagine host. La chiave ora comprende `levels`; modifica di correttezza da verificare visivamente, senza attribuirle già la causa del nero in combattimento.
- `rsx_fp_apply_unnorm_scale()` modifica solo chiamate `Sample`, non `SampleBias` o `SampleLevel`. Uno shader che usa coordinate non normalizzate insieme a TXB/TXL campionerebbe con coordinate errate. Non sono presenti tali chiamate negli HLSL precompilati attuali di DBZ3.

Il backend deko3d mantiene le texture rilasciate finché la GPU ha finito di usarle e attende prima di riciclare la memoria di upload. L'ispezione non ha trovato un riciclo anticipato evidente; il comportamento va comunque confrontato con una cattura nel momento del difetto. `tex_fail=0` e oltre 300 MiB liberi negli intervalli di `run15` non escludono un alias o dati obsoleti nella cache.

La correzione della chiave è inclusa nell'NSP di sviluppo `graphics-memory-key` del 27 ottobre 2026 (`build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp`). Compilazione e pacchettizzazione Switch completate; serve ancora la prova visiva durante un combattimento in cui il difetto era riproducibile.

## Log letti dalla Switch durante la prova

`build/logs/dbz3/dev/run16` è stato copiato dalla console. `t19-stderr.log` identifica una build compilata alle 00:02:38, precedente all'NSP `graphics-memory-key` pacchettizzato alle 00:46:13. `t22-gpu.log` è accodato: i primi 420.760 byte sono identici a `run15`; i 50.573 byte successivi comprendono due avvii. Nell'ultimo avvio, corrispondente al `t19-stderr` di `run16`, i sette campioni fino a 6.300 present mostrano al massimo 410/1024 texture residenti, almeno 348 MiB liberi e `tex_fail=0`. Nella porzione nuova dell'ultimo avvio non compaiono pipeline rifiutate, draw saltati o errori della coda GPU. Questi contatori misurano capacità e fallimenti espliciti; non verificano che i byte campionati da uno shader siano ancora quelli giusti.

## Catture Switch del 28 ottobre 2026: payload leggibili, render corrotto

Le nuove catture sono in `userdata/screenshoot/switch-dbi-import/raw/game/draw-captures` e le immagini finali nella cartella `png` corrispondente. Per le tre schermate di Continue, Save e Dragon Universe abbiamo esportato e decodificato i byte originali RSX:

- `frame_19723_1793185855`, draw 74 e 75: `0x050DC700` e `0x050DD000`, DXT3 da 64x32 e 32x32. I file raw decodificano chiaramente in `Yes` e `No`, ma il target finale mostra lettere spezzate e blocchi di altre texture.
- `frame_29104_1793186151`, draw 229 e 230: `0x04951080` e `0x0494EE80`, DXT3 256x32. I payload decodificano in `No` e `Yes`; il prompt finale resta corrotto.
- `frame_2814_1793185307`, texture `0x04C9CB00`, DXT3 64x32: il contenuto decodifica in un atlas coerente di terreno/scarpata/acqua, mentre l'ambiente presenta bande nere e tasselli.

Tutti questi draw sono in memoria locale e i capture mostrano la risoluzione offset→EA con hash completo e byte leggibili. I payload Yes/No sono compatibili con il contenuto atteso, ma un atlas decodificabile non dimostra che sia la risorsa o la versione giusta per quel draw. Non sono ancora verificati i byte effettivamente campionati dalla GPU e le coordinate finali dopo il vertex shader. Anche la riga Story Reenacted è stata verificata nel payload: draw 488 (`0x051A7900`) contiene `Voice Data (original story)`, draw 489 (`0x0519F500`) contiene `#` e i draw 490–492 (`0x0518ED00`) fanno riferimento all'atlante delle cifre. Il render finale mostra frammenti verticali sovrapposti a quel testo. Le texture 512x512 dei draw 486–487 superano il limite del dump e restano da verificare per gli altri elementi del pannello.

Nelle catture iniziali con `start 0`, i checkpoint immagine mostravano solo i primi draw, mentre `draws.txt` registrava l'intero frame. Nella specifica cattura Save `frame_29104_1793186151`, il testo è ai draw 229–230 e il compositing al 231; nella cattura Continue `frame_19723_1793185855`, le scritte sono ai draw 74–75. Questi indici hanno permesso di identificare i payload, ma variano con scena e frame e non sono riferimenti stabili per avviare una nuova cattura. Il nuovo tentativo mirato è descritto sotto.

## Catture mirate successive (28 ottobre 2026)

Importati `frame_2169_1793187502` e `frame_3391_1793187543`, entrambi con `start 224; step 1`. I log contengono rispettivamente 184 e 342 draw. Il primo non arriva al draw 224; nel secondo i draw 229–230 sono ancora geometria 3D della mappa, con texture a `0x062D3C80` e `0x062D3300`, non le scritte Yes/No del menu. Il target cambia a `0x00BB0000` dal draw 254, ma il capture non contiene checkpoint `.ppm` né `final.ppm` per questi due frame. Quindi questa prova non ha isolato il prompt Save e non consente un confronto visivo per pass.

Entrambi i frame ripetono al draw 16 il binding `0x04C9CB00`, formato `f87`, 64x32, hash `63937A223EE17357`, già visto nella cattura della scarpata. Il payload è presente in entrambi i dump. La corrispondenza conferma che la texture sorgente e il binding osservato sono stabili fra questi due frame; da sola non identifica un errore nell'upload o nel campionamento. Gli indici di draw sono dipendenti dalla scena e non identificano lo stesso oggetto tra frame diversi: per catturare il menu occorre avviare la registrazione mentre il prompt è visibile e verificare il contenuto delle draw, senza assumere che 224–231 siano gli stessi pass.

## Implementazione: provenienza della texture effettivamente bindata

Ho esteso `draws.txt` per collegare ogni texture del draw al suo handle backend e alla voce corrispondente nella cache: `bind=<handle>` e `resident=<handle>:<hash>`. Durante la cattura viene scritto anche l'hash completo dei byte sorgente per ogni dimensione, non solo il campione delle texture grandi. Questo confronta nello stesso record i byte attualmente raggiunti dall'indirizzo guest, il contenuto che la cache ritiene caricato e l'oggetto dato al draw.

Piano basato su questa correlazione:

1. Se l'hash sorgente differisce da `resident`, correggere la rilevazione/invalidazione della memoria modificata.
2. Se l'hash sorgente è quello di un'altra risorsa, correggere risoluzione EA o proprietà/durata dell'allocazione guest.
3. Se gli hash coincidono ma il draw usa un `bind` diverso dalla voce `resident`, identificare prima un eventuale alias legittimo di render target/depth; correggere l'associazione solo se la risorsa scelta è errata.
4. Se indirizzo, hash e handle coincidono ma il target resta sbagliato, verificare conversione del payload, formato immagine e descriptor Deko3D. Non cambiare shader senza questa evidenza.

La cattura resta attivata solo dal menu debug; l'hash completo aggiunge costo solo durante quel singolo frame di indagine.

## Folla Tenkaichi, regressione e budget memoria

L'utente osserva fotogrammi estranei nei pannelli animati della folla e riferisce che nelle build precedenti il difetto era limitato all'alpha. Trattare questo come indizio di regressione; manca ancora l'identificazione della precisa build precedente funzionante. Nel percorso `eng_texture_upload` ogni texture guest viene decodificata e caricata separatamente, con i suoi mip: non viene costruito un nuovo atlas che impacchetta insieme le texture. Il contenuto di un atlas può comunque essere sbagliato in memoria guest, vecchio nella cache o letto con layout/coordinate sbagliati.

Il backend riserva `DK_IMAGE_POOL = 512 MiB` a immagini texture e render target, `DK_MAX_TEXTURES = 1024` slot texture e 8 MiB allo staging di upload/readback. Lo staging è un buffer di trasferimento riutilizzabile, non il limite cumulativo delle texture. Nell'ultimo avvio presente in `run46/t22-gpu.log`, i campioni da 900 a 7200 present riportano massimo 408 texture, minimo 375 MiB liberi, blocco libero maggiore minimo 375 MiB e `tex_fail=0`. I contatori vengono scritti ogni 900 present anche con diagnostica verbosa spenta: non misurano ogni draw né escludono picchi intermedi, errori di vita delle risorse o letture da una risorsa sbagliata. Il log è accodato fra avvii.

Ordine dell'indagine, usando prima codice, cronologia e materiale già disponibile:

1. Ricostruire le differenze rispetto alla build precedente funzionante, concentrandosi su cache texture, upload, binding e dati UV/costanti. Le ottimizzazioni di hash parziale e upload differito compaiono già nel commit iniziale `2e34739`; la loro presenza da sola non identifica una regressione recente.
2. Fix nella build `surface-layout-binding`: `sink_bind_textures` riusa una superficie solo se formato, dimensioni, pitch e layout lineare corrispondono. Il controllo copre RGBA8, RGBA16F, RGBA32F e R32F. Per una superficie RGBA8 vista con descriptor incompatibile, sincronizza i byte guest e decodifica il formato richiesto; un binding depth non può ricadere sulla superficie colore allo stesso indirizzo.
3. La build precedente `animated-texture-refresh` è stata superseduta: il controllo completo per frame poteva aumentare il carico durante le mosse con molti effetti. La build corrente mantiene il controllo periodico originale per ridurre il rischio sul frame pacing.
4. Confrontare per la draw interessata sorgente, contenuto GPU e coordinate dopo il vertex shader. Il capture `bind/resident` copre l'associazione CPU, non sostituisce il readback dell'immagine GPU o la verifica degli UV trasformati.

Non aumentare il pool sulla base dei dati attuali. Il criterio per il fix è eliminare il primo scostamento dimostrato fra risorsa richiesta e risorsa campionata, preservando l'animazione della folla e i menu.

La revisione estesa di `surface-layout-binding` è stata compilata e pacchettizzata il 28 ottobre 2026 alle 15:26. ELF SHA256: `71E00F9F801EF3034F31C5C4C1B0C38F56591373BCE8D0017254B54498860EA0`; NSP SHA256: `9FE92552C620354452FA889363ADCDCC1CF473537D57993EE7419EF78BB1013E`. Il miglioramento dell'aura Kaiohken conferma il percorso RGBA8; questa revisione aggiunge il binding diretto compatibile per le superfici floating point, possibile fonte degli artefatti rimasti o introdotti. La scena non è stata verificata in gioco dopo questa revisione. Il backend Bink usa il tempo monotono reale; non ho trovato log recenti che colleghino la deriva audio/video a un errore dell'orologio. Il pacchetto continua a escludere il refresh texture costoso.

## Prossima cattura utile

Per attribuire una causa alla folla o a un effetto servono, nella stessa scena e nella stessa build, un'immagine del difetto e il draw che lo produce. Registrare per quel draw: frame, shader VS/PS, texture unit e byte della texture, dimensioni, formato e mip, UV, wrap, remap, alpha test, blend RGB/alpha e target prima/dopo il passaggio. La cattura per passaggi `RSX_ENGINE_PASS_DUMP` e `RSX_ENGINE_PASS_DUMP_DRAWS` esiste già ma va attivata mentre il difetto è visibile; l'indice `t28` e i normali log non contengono queste associazioni. Confrontare quindi il draw con la resa attesa e correggere il primo stadio che diverge: dati texture, coordinate/sampler, shader o compositing.

## Criterio ricorrente nei difetti attuali

I due casi con la correlazione più forte condividono il formato: scarpata in `frame_3518_1793181443`, draw 12, texture `0x04C9CB00`, formato RSX `0x87` (DXT3/BC2), 64x32; testo `Yes/No` nei draw del prompt Continue e Save, ancora formato `0x87`. Per la scarpata le UV riportate restano in [0,1], il sampler usa wrap 3 (clamp) e le bande sono già nel target subito dopo la draw 12. I dump sorgente decodificano in modo coerente, compresi i testi `Yes` e `No`, mentre la resa Switch li frammenta. Questi dati non indicano un indirizzo guest sbagliato né un errore di compositing successivo alla draw.

Il criterio ripetibile finora è quindi il percorso di campionamento delle texture DXT3/BC2 nel backend Deko3D, non l'indirizzo della texture. È un'ipotesi circoscritta e falsificabile, non ancora una diagnosi per tutti i materiali: manca un confronto del medesimo draw e della stessa texture sul backend PC/PS3. La build `bc2-rgba8-upload` prova il bypass del campionamento compresso nativo per BC2 fino a 16 KiB: espande gli stessi byte guest in RGBA8 all'upload, lasciando invariati indirizzo, cache, UV, remap e sampler. Il limite tiene contenuto l'aumento di memoria immagine; il fallback non copre BC2 più grandi. Se cambiano scarpata e testi Save/Continue, il difetto è nel percorso BC2 nativo; se non cambiano, il sospetto torna al descriptor o allo shader della draw.

Build del 28 ottobre 2026, pacchetto preparato per questa prova: ELF SHA256 `5336800D70B3D31165281499492B4BB1254FE8BFE4B6B4BFD5302CFEEF87E18B`; NSP SHA256 `903B0881295C98D62AC66A7D5CB8789561437540A64053CA936D0B467EDAAA5F`. Non è stata ancora verificata in gioco.
