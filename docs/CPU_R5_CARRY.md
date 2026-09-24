# CPU r5 source carry boundary

This component is a narrow compiled-source compatibility boundary for the
near-zero CPU stick path. It carries a typed source word through one live
fighter allocation and accepts the seed-global route only after the owned DOL,
disc/apploader and pinned source adapter derives it. The C sidecar is a trusted
caller boundary: `independently_derived` is an attestation supplied by that
adapter, not proof that the sidecar inspected a DOL. Synthetic source words
remain useful for unit tests and do not establish retail provenance.

World generation, allocation generation, source fighter identity and a
monotonic sidecar lifetime are checked on every transition. Allocation
generation is required because a fighter address can be reused within one
world, and a fresh sidecar must reject an old token even when its local
lifetime counter restarts. The source fighter identity must come from the
authoritative live allocation binding; the current focused test supplies a
relocated synthetic fighter identity while separately exercising the owned
seed derivation.

The sidecar preserves unknown values and explicit zero. It has no fallback for
floor-query, standings-object or other source stack/object values, and those
routes remain unsupported until their source reaching definitions are bound.
Each CPU dispatch must begin or invalidate its carry according to the audited
source path; a previous tick's seed must never survive an unknown next call
path. The current source audit identifies the HSD_Randf seed path in
`src/melee/ft/kinds/ftCommon/ftCo_0A01.c` and the state-18 dispatch to
`ftCo_800AC5A0`, but no production publish/consume hook has been enabled.

The focused source run compiles the untouched `random.c` with the pinned
Wasm32 SDK, `-ffp-contract=off` and `-Werror`, and records the random-source
hash before and after. The owned-input receipt is retained under the ignored
`work/cpu-r5-carry-evidence/` directory; it contains DOL, symbol, disc-header
and apploader identities without personal paths or captured CPU output.

Run the focused checks after provisioning the pinned source, DOL, disc and
Wasm32 SDK. The four owned CPU inputs are intentionally explicit; the adapter
does not read a profile JSON or captured CPU row:

```sh
MELEE_EMSDK=/path/to/emsdk \
MELEE_PINNED_SOURCE=/path/to/melee \
MELEE_CPU_DOL=/path/to/main.dol \
MELEE_CPU_DISC=/path/to/owned.ciso \
MELEE_CPU_SOURCE_ROOT=/path/to/melee \
MELEE_CPU_SYMBOLS=/path/to/melee/config/GALE01/symbols.txt \
python3 -m unittest tests.test_cpu_r5_source_context \
  tests.test_gameplay_cpu_r5_carry -v
```

The latest owned-input receipt is retained at
`work/cpu-r5-carry-evidence/run-_rif2kw5/receipt.json`. The provisioned full
suite receipt and log are
`work/cpu-r5-carry-evidence/full-suite-owned-provisioned-v1.json` and
`work/cpu-r5-carry-evidence/full-suite-owned-provisioned-v1.log`.

This is compiled and source-adapter evidence only. It does not establish a
live browser gameplay result, general CPU compatibility, floor/standings
coverage, or whole-session equivalence. The remaining production work is to
derive the live fighter allocation binding and add narrowly audited source
publish/consume hooks at the relevant CPU dispatch boundaries.
