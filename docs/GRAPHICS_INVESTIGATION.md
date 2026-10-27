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

- `dk_wrap()` in `patches/ps3recomp-overlay/libs/video/rsx_deko3d_backend.c` appiattisce i modi RSX 5–8 in clamp to edge, benché deko3d supporti clamp e mirror clamp. Non spiega S/T delle texture già catturate, che usano soltanto 1 e 3.
- La chiave della cache in `third_party/ps3recomp/libs/video/rsx_draw_engine.c:eng_texture_slot()` non comprendeva il numero di mipmap. Due binding dello stesso indirizzo e formato con catene mip diverse potevano quindi riutilizzare la stessa immagine host. La chiave ora comprende `levels`; modifica di correttezza da verificare visivamente, senza attribuirle già la causa del nero in combattimento.
- `rsx_fp_apply_unnorm_scale()` modifica solo chiamate `Sample`, non `SampleBias` o `SampleLevel`. Uno shader che usa coordinate non normalizzate insieme a TXB/TXL campionerebbe con coordinate errate. Non sono presenti tali chiamate negli HLSL precompilati attuali di DBZ3.

Il backend deko3d mantiene le texture rilasciate finché la GPU ha finito di usarle e attende prima di riciclare la memoria di upload. L'ispezione non ha trovato un riciclo anticipato evidente; il comportamento va comunque confrontato con una cattura nel momento del difetto. `tex_fail=0` e oltre 300 MiB liberi negli intervalli di `run15` non escludono un alias o dati obsoleti nella cache.

La correzione della chiave è inclusa nell'NSP di sviluppo `graphics-memory-key` del 27 ottobre 2026 (`build/dev/dbz3/Dragonball-Z-Budokai-3-dev.nsp`). Compilazione e pacchettizzazione Switch completate; serve ancora la prova visiva durante un combattimento in cui il difetto era riproducibile.

## Log letti dalla Switch durante la prova

`build/logs/dbz3/dev/run16` è stato copiato dalla console. `t19-stderr.log` identifica una build compilata alle 00:02:38, precedente all'NSP `graphics-memory-key` pacchettizzato alle 00:46:13. `t22-gpu.log` è accodato: i primi 420.760 byte sono identici a `run15`; i 50.573 byte successivi comprendono due avvii. Nell'ultimo avvio, corrispondente al `t19-stderr` di `run16`, i sette campioni fino a 6.300 present mostrano al massimo 410/1024 texture residenti, almeno 348 MiB liberi e `tex_fail=0`. Nella porzione nuova dell'ultimo avvio non compaiono pipeline rifiutate, draw saltati o errori della coda GPU. Questi contatori misurano capacità e fallimenti espliciti; non verificano che i byte campionati da uno shader siano ancora quelli giusti.

## Prossima cattura utile

Per attribuire una causa alla folla o a un effetto servono, nella stessa scena e nella stessa build, un'immagine del difetto e il draw che lo produce. Registrare per quel draw: frame, shader VS/PS, texture unit e byte della texture, dimensioni, formato e mip, UV, wrap, remap, alpha test, blend RGB/alpha e target prima/dopo il passaggio. La cattura per passaggi `RSX_ENGINE_PASS_DUMP` e `RSX_ENGINE_PASS_DUMP_DRAWS` esiste già ma va attivata mentre il difetto è visibile; l'indice `t28` e i normali log non contengono queste associazioni. Confrontare quindi il draw con la resa attesa e correggere il primo stadio che diverge: dati texture, coordinate/sampler, shader o compositing.
