# Early content checks

Use `scripts/check_content.py` during fighter/stage implementation, before a long
browser replay. It batches explicit asset checks, configures the pinned source
build, builds only the selected lifecycle targets, and runs them in order. The
first failing step stops the sequence and retains its log and a compact report.
Missing inputs fail this command; they never become an optional test skip.

This wraps existing probes. It does not infer dependencies, enable a registry
row, extract a new source contract, or establish content admission. Start with
[fighter checkpoints](ADDING_CHARACTERS.md) or [stage requirements](ADDING_STAGES.md)
and derive each new content inventory from its original source and owned assets.

## Local inventory

Keep the manifest under ignored `work/`. Paths may be absolute or relative to
the manifest file. `checks` names exact parser operations; `lifecycles` names
supported source trace contracts. Neither list is a complete dependency graph:
the lifecycle directories must contain the original common, fighter, stage,
effect, HUD, font and audio inputs required by the selected runtime.
Select parser rows appropriate to the boundary being changed. The inspection
model parser has narrower capabilities than the original native HSD runtime;
its rejection is parser evidence, not proof that the source stage cannot run.
Retain such failures and use the stage-specific trace for source lifecycle claims.
For lifecycle-only work, explicitly set `"checks": []`; the report lists asset
parser checks as unverified and no parser is compiled or run. A nonempty `checks`
array still runs first, and any selected parser failure stops the combined run.
Use the standalone batch command below when only the parser boundary is changing.

For example, `work/content-check.json` can contain the following **bounded sample**
for Dr. Mario/Roy on Final Destination and the separate Battlefield trace:

```json
{
  "checks": [
    {"path": "../assets-local/next-gate/PlDrNr.dat", "symbol": "PlyDrmario5K_Share_joint"},
    {"path": "../assets-local/next-gate/PlFeNr.dat", "symbol": "PlyEmblem5K_Share_joint"}
  ],
  "lifecycles": [
    {
      "target": "gameplay_content_match_trace",
      "menus": "../assets-local/native-menus",
      "assets": "../assets-local/next-gate",
      "stkind": 32,
      "p1_ckind": 22,
      "p2_ckind": 23
    },
    {
      "target": "gameplay_stage_battlefield_trace",
      "assets": "../assets-local/next-gate"
    }
  ]
}
```

These numbers are original `StKind` and character-select `CKind` identities,
not ground kinds, fighter kinds or menu tile indices. Extend `checks` with each
required costume/root or stage map entry; the two model rows above do not claim
complete costume parsing. The source trace itself reconstructs every selected
P1 costume, and the command runs both player orientations (once for a self-match).
Only the action branches actually implemented in that trace are exercised.
For example, its Yoshi's Story branch checks stage Articles instead of executing
the special-move branch. A new family may require extending that trace first.

The Battlefield contract is explicitly stage-specific: two owner lifetimes,
7,200 ticks each, with the existing scheduler, geometry and teardown assertions.
It cannot validate another stage by substituting its DAT. Add a reviewed trace
contract for another stage's unique services instead of assigning it this scope.

## Run and inspect

```sh
python3 scripts/check_content.py --manifest work/content-check.json \
  --out work/content-check-01 --jobs 2
```

The default configuration is Release; `--configuration RelWithDebInfo` selects
the other explicit build directory. There is no selection by file timestamp
and no option to skip the build. The command delegates source/toolchain checks,
configuration and compilation to `scripts/build.py`, recording them in `build.log`.
Ninja reuses current objects normally; unchanged generated ImGui source retains
its timestamp so configuration alone does not force a backend rebuild and relink.
The output directory must be new, under `work/`, and outside input directories.

The same selected-target build is available directly when debugging a trace:

```sh
python3 scripts/build.py --configuration Release --jobs 2 \
  --trace-target gameplay_stage_battlefield_trace
```

Repeat `--trace-target` to build both supported traces. This selects only the
named targets and their dependencies. It cannot be combined with a `--target`
group or a public/provenance/selective build mode.

`report.json` includes input hashes, the initial Git revision/diff hash,
dependency lock, executed JS/Wasm hashes, exact commands, step durations,
scoped lifecycle selections, pending runs and the first failed step's diagnostic
tail. Rejected parser rows retain their requested symbol/entry selections. Full
logs remain alongside the report. Input files are hashed again after execution;
changed inputs invalidate the result and retain before/after hashes. Directory inventories include files not
necessarily consumed by the selected trace; hashes establish observed identity,
not a claim that every file was exercised or verified against retail.

Read the failure's `next_check` and complete log before retrying. A missing
source service or unsupported family remains a failure; do not add an exemption
or increase the timeout to turn it into a pass. Commands never retry a failing
row automatically, and completed earlier rows remain visible.

A pass covers the declared parser and source lifecycle checks only. Original
CSS/SSS input, full action/stage coverage, retail state/draw comparisons, pixels,
PCM fidelity and cold/warm live timing remain separate checks. See the
[developer entry](DEVELOPMENT.md) for the next boundary and integration validation.

## Batch parser checks alone

`scripts/check_assets.py --manifest work/asset-checks.json` accepts the same
`checks` array in an object containing only that field. Each row requires `path`
and may supply `symbol`, unsigned `stage_entry`, and boolean `opaque`.
`opaque: true` requires a stage entry and retains the existing explicit partial
render-pass scope. All rows are validated before the checker is compiled once.
Parser rejections are retained while the rest of the batch is checked.
Positional path/directory checks and their existing flags still work.
