# Switch shader sources

`deko3d/hlsl/` contains the shader sources captured by the Deko3D backend on
Switch. These hash-named HLSL files are the inputs to the PC shader compiler
and can be committed to the project. `deko3d/hlsl/override/` contains manual
fixes for individual captures.

The build generates GLSL and a compiled C header locally. Those outputs are
ignored by Git. To import another Switch run over DBI MTP, start **Run MTP
responder** on the console and run `scripts/collect-switch-shaders.ps1` from
the project root. Then rebuild the NSP with `scripts/build/switch-dev.ps1`.
