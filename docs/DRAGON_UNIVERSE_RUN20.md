# Cattura grafica Dragon Universe, run20

La cattura `build/logs/dbz3/dev/run20/map-pass-dump` riguarda la mappa al frame 3943. La pelle del personaggio appare già bianca in `pass000_t0.ppm`, prima del postprocessing dell'acqua. La costa è ancora regolare in quel target e si deforma in `pass003_t0.ppm`: il passaggio 003 usa la pipeline 111 e campiona la scena a `0x007D0000` e la maschera a `0x03E20000`, prodotta nel passaggio 002.

Il passaggio 001 campiona l'indirizzo zeta `0x003F0000` e produce un'immagine nera. Il backend deko3d restituisce zero da `be_depth_snapshot()` e non implementa `depth_readback`. È necessario verificare il contributo di questa mancanza alla maschera e alla distorsione. Il dump attuale registra gli ID delle pipeline ma non i loro hash VS/PS.

La build diagnostica `map-pipeline-ids` aggiunge `map-pass-dump/pipelines-deko.txt` quando la cattura è attiva. Il file collega gli ID dei draw agli shader precompilati, senza richiedere `diag.on`. Dopo il prossimo test si potrà attribuire lo shader del personaggio e quello della distorsione prima di modificarli.

Un report Atmosphère nuovo, `01793089936_0100bdba30968000.log`, risolve il PC a `vm_read32` chiamata da `func_001D1EC0` nel task DU `func_001DBC58`; l'indirizzo guest letto è `0x2F41A00C`. È distinto dal crash precedente della schermata livello. L'origine del puntatore non è ancora nota.
