# Cattura dei draw dalla Switch

Questa funzione acquisisce un solo frame dal renderer RSX quando viene avviata dal menu debug. Produce un elenco di tutte le draw call eseguite, fino a 16 immagini progressive del target colore e un'immagine finale. È pensata per localizzare il primo draw che rende un glifo o un materiale in modo errato.

## Uso

1. Aprire nel gioco una schermata stabile con il difetto, per esempio il menu di pausa con testo corrotto.
2. Aprire il menu debug con **L + click stick sinistro + click stick destro**.
3. Impostare **DRAW STEP: 8** e **DRAW START: 0**. Su DRAW START, Sinistra/Destra spostano l'indice di un passo; ZL/ZR lo spostano di 128. Su DRAW STEP, A o Sinistra/Destra alternano 8 e 1.
4. Selezionare **CAPTURE NEXT DRAW FRAME** e premere A. Il menu si chiude; viene catturato il frame successivo. Il gioco può fermarsi per alcuni secondi durante i readback.
5. Con la Switch in **DBI > Run MTP responder** e collegata via USB, eseguire dal progetto `.\scripts\import-switch-images.ps1`. Lo script copia le catture e converte automaticamente le immagini in PNG.

Le immagini convertite si trovano in `userdata/screenshoot/switch-dbi-import/png/game/draw-captures/frame_<numero>_<ora>/`. I file originali e `draws.txt` sono in `userdata/screenshoot/switch-dbi-import/raw/game/draw-captures/frame_<numero>_<ora>/`. L'importazione include anche le immagini statiche in `captures` e nell'Album della Switch; con `-SkipAlbum` si limita alle cartelle del gioco. I video MJPEG vengono ignorati normalmente; `-IncludeRecordings` li copia e ne estrae i frame PNG. Serve `ffmpeg` nel PATH. Rieseguire lo script importa solo i file nuovi o cambiati.

`draws.txt` contiene un record per draw eseguito: indice, target e dimensioni, pipeline, stato colore/blend/depth/alpha, texture, piu gli attributi dei primi tre vertici. Per ogni texture include anche l'indirizzo guest risolto (`offset->EA`), l'intervallo di byte, l'hash dei byte sorgente, l'handle effettivamente associato al draw (`bind`) e l'handle/hash conservati nella cache (`resident`). Durante una cattura l'hash sorgente e completo anche per le texture grandi. Se `resident` esiste e il suo hash differisce da quello sorgente, la cache sta conservando contenuto diverso da quello attualmente in memoria. Un handle `bind` diverso da `resident` va letto insieme alla risorsa: puo indicare una superficie renderizzata o una snapshot di profondita invece della normale texture guest. `resident=0` significa che non e stata trovata una voce nella cache delle texture guest. `drawNNNNN_t0.ppm` e il target colore immediatamente dopo quel draw. `final.ppm` e il target presentato alla fine del frame. La cattura salva un'immagine ogni DRAW STEP draw, a partire da DRAW START, fino a 16 immagini.

La build `texture-ea-hash-draw-capture` salva anche i byte sorgente delle texture legate ai draw: solo texture fino a 64 KiB, massimo 64 risorse e 4 MiB per cattura. `draws.txt` riporta il nome del file con `raw=...`; i file `.bin` sono payload originali RSX, prima del decode del renderer. Per texture DXT1/3/5 si possono decodificare in PNG con `python scripts/t28_decode.py <cartella-cattura>`; usa Pillow quando disponibile e FFmpeg come ripiego. Il dump parte solo quando si arma una cattura dal menu debug.

`scripts/import-switch-images.ps1` importa anche questi `.bin` dalle cartelle `draw-captures` e non li converte automaticamente. La build diagnostica piÃ¹ recente, `texture-source-dump`, estende il dump alle stesse condizioni e limiti.

La build `visual-cliff-diagnostics` aggiunge a ogni riga `prim`, `refs`, `indices`, `cuts`, hash del vertex program, indirizzo e hash strutturale del fragment program, oltre a `col3_minmax` e `dark_verts` per il colore dei vertici. Questi campi servono a distinguere un errore di topologia o colore da uno nello shader o nel campionamento della texture.

La build `du-map-draw-diagnostics` estende le catture armate dal menu: `a0_bbox` riporta i limiti XYZ dei vertici di ogni draw, così i draw della mappa si possono raggruppare per area; i fragment program a 14 costanti riportano tutte le costanti effettive del draw e salvano il bytecode `fp_*.bin`; i vertex program distinti vengono salvati come `vp_*.bin`. Questi file vengono scritti solo durante una cattura esplicita. Servono a verificare se una tile ha draw/vertici associati e a ricostruire shader e parametri del materiale della pelle.

Se i glifi guasti compaiono fra due immagini consecutive, spostare DRAW START all'inizio di quell'intervallo e ripetere con DRAW STEP 1. Se le prime 16 immagini terminano prima che il menu venga disegnato, aumentare DRAW START di 128 e ripetere. L'indice ricomincia da zero a ogni frame e conta i draw effettivamente inviati al backend; i draw scartati perché non hanno pipeline o vertici validi non compaiono nel file.

Questa prima versione salva i passaggi su SD. Non congela né riproduce l'intero stato GPU come il Frame Debugger di Unity: per confrontare indici fra catture, mantenere la stessa scena ferma. Il formato PPM può essere aperto con strumenti come ImageMagick o convertito in PNG senza perdita.

Il menu debug include anche `GPU FXAA`, disattivato per default. Se attivato, applica un pass FXAA GPU al target finale prima della presentazione; il costo si aggiunge a ogni frame. La scelta viene salvata in `debug-settings.cfg`. FXAA smussa i bordi ad alto contrasto, ma non corregge shader, texture o geometria mancanti.
