# Dragon Universe: cattura run21 e build di prova

La cattura `build/logs/dbz3/dev/run21/map-pass-dump` proviene dalla build
`map-pipeline-ids`. Il personaggio ha la pelle bianca in `pass000_t0.ppm`,
prima degli effetti acqua. La deformazione di costa e personaggio compare in
`pass003_t0.ppm` e resta nel passaggio successivo.

`pipelines-deko.txt` associa i draw 146/147 del personaggio allo shader
`ps_dcfe671525a85775` e il passaggio 003 (pipeline 118) allo shader
`ps_43b4c9959faacec2`. Il primo è la variante a 14 costanti dello shader
corpo a 10 costanti `ps_511e3e7252f5f9dc`, per il quale esiste già un
workaround di flusso di controllo. Il secondo usa la maschera a risoluzione
640x360 del passaggio 002 per spostare le coordinate di campionamento della
scena a 1280x720.

La build `map-shader-guards` applica lo stesso workaround di flusso di
controllo alla variante del corpo usata nella mappa. Nell'effetto acqua salva
il canale blu della maschera al pixel originale e campiona la scena senza
spostamento dove quel canale è inferiore a 0,5. La soglia separa il blu
dell'acqua dall'ocra della terra nella maschera catturata. Il codice shader
è compilato (47 shader, zero errori); l'effetto visivo e la stabilità devono
essere ancora verificati sulla console.

La build è sulla SD come
`switch/dbz-budokai-3-switch/Dragonball-Z-Budokai-3-map-shader-guards.nsp`.
Il file è stato letto di nuovo via MTP: SHA-256
`4384B0B49102EB569F61BB61B12FF7F6B37F7419120C9B5402ACC14AE5E2800F`.
Il marker `map-capture-after` resta impostato a 120 secondi; la cattura
grafica può rallentare temporaneamente il gioco durante quel momento.

Da verificare: pelle di Vegeta in mappa, costa e altri oggetti vicino
all'acqua, movimento dell'acqua, combattimento e schermata di aumento livello
dopo una vittoria. Il crash DU precedente nel task `0x001DBC58` è distinto
da queste modifiche shader e resta da indagare se si ripresenta.
