# Source-derived Synth startup parameters

The parameter probe executes the original `fn_80023254` selection logic and the
bank-size statements in `lbAudioAx_8002838C`. A typed observation boundary captures
the authored `AXDriver_8038E498` call's arguments. It does not invoke AXDriver or
Synth initialization. The [scoped receipt](evidence/source-synth-parameters-v1.json)
records source execution and the separate original bank-size comparison.

The owned-input path verifies the revision-2 DOL, clean pinned source checkout,
symbol map, table declarations and their original executable bytes. The table
named `s32_arr_803BB5D0` is actually an authored `s8[56][4]`; its final implicit
zero row follows C initialization. Table extents and signedness are checked before
hydration. Captured allocation values are never inputs to the probe.

```sh
python3 tools/source_synth_parameters.py \
  --dol /path/to/owned/main.dol \
  --symbols .deps/melee/config/GALE01/symbols.txt \
  --source-root .deps/melee \
  --work-dir work/synth-parameters-new
python3 -m unittest discover -s tests -p test_source_synth_parameters.py -v
```

The artifact directory must be new or empty. Generated C, compiler commands,
checked Wasm, stdout, stderr and timeouts remain on disk. The fixture uses the
original four-byte MSL boolean declaration, Wasm32 pointers, checked memory and
unaltered source arithmetic. Source identity is checked before and after the
owned derivation. Tests also execute the authored source initializers without a
private DOL; that path is source-only evidence, not an owned executable comparison.
`MELEE_WEB_OWNED_DOL` enables the separate local DOL tests.

The observed driver arguments and bank total are derived outputs. The comparison
reads the completed output and original capture separately, then checks the one
bank reservation size. It does not validate AR allocation addresses, callback
ordering, complete audio initialization, PCM, browser behavior or session
equivalence. The next step passes these parameters into original Synth startup
with the shared AR/ARQ and [AX services](SOURCE_AX_STARTUP.md).
