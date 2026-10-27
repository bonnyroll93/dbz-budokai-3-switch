# Dragon Universe: run22, Namek e prova grafica

I log `build/logs/dbz3/dev/run22` appartengono alla build compilata il
27 ottobre 2026 alle 11:33:24 (`map-shader-guards`). L'utente conferma che
il difetto grafico rimane e segnala un crash nella storia di Goku entrando
su Namek.

Il nuovo report Atmosphère `01793091304_0100bdba30968000.log` termina in
`vm_read32`, chiamata da `func_001D1EC0` attraverso `func_001D48B4` e il task
DU `func_001DBC58`. Il puntatore della lista effetti è `0x2F41A000`;
la lettura a `+0xC` tenta `0x2F41A00C`, nell'area di controllo RSX non mappata.
Lo stesso indirizzo appare nel report precedente `01793089936...`.
`scripts/postprocess-lift.py` aggiunge un controllo ai nodi iniziali e ai
link successivi della lista. Un nodo non valido viene registrato e saltato;
non è ancora dimostrato perché la lista contenga quel valore, né che la
storia prosegua correttamente dopo il salto.

La cartella `map-pass-dump` copiata in run22 è **stale**: `passes.txt` inizia
ancora al frame 3899, 120 secondi dall'avvio, come run21. L'ultima prova è
terminata intorno a 45 secondi e non ha prodotto una nuova cattura. Non si
può usare quell'immagine per giudicare la patch precedente. La prova nuova
usa il workaround completo dello shader corpo e, temporaneamente, campiona
la scena senza distorsione nel passaggio acqua. La texture base dell'acqua
resta visibile; la rifrazione richiederà la maschera/profondità corretta.

NSP di prova sulla SD:
`switch/dbz-budokai-3-switch/Dragonball-Z-Budokai-3-namek-guard-water-copy.nsp`.
SHA-256 verificato con lettura MTP:
`4756FAA3C72FCB960355B31D27782785FDEBD67B71CEAF057CC906780532C468`.

Nel profilo `t26-loop-prof.log` ci sono finestre DU con 23 passaggi grafici
in circa cinque secondi e solo 18,2 passi logici/s: il limite corrente di
quattro passi logici per immagine impedisce di recuperare 60 Hz quando il
framerate scende molto sotto 15 FPS. Altre finestre DU arrivano a circa
40,8 e 55,0 passi/s. Questo è distinto dal caso in cui un task di disegno
contiene logica e viene eseguito una sola volta per immagine. Occorre
misurare entrambe le cause per scena prima di estendere il recupero.
