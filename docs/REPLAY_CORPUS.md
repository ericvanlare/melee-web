# Vanilla replay corpus

This corpus extends the [complete-game calibration](COMPLETE_REPLAY_CALIBRATION.md)
with independent input workloads. Slippi files supply controller intent under a
named conversion policy. Newly executed vanilla Melee supplies expected state.
The original recording's post-frame positions, damage, RNG and outcome are
never substituted for that execution.

## Selection and split

Freeze development and held-out membership by the complete donor SHA-256 before
running the port. A shorter prefix, renamed file or second reference capture of
the same donor remains the same workload. Keep held-out inputs out of coverage
selection, pipeline discovery and runtime debugging until the development build
and seed are frozen. Evaluate them against that frozen build. If a held-out
failure is used to make a fix, retain the failure, promote that source to
development and reserve a fresh untouched source before claiming another
held-out pass.

Use headers and input availability to reject unusable donors cheaply. Among
eligible development candidates, measure coverage from the new vanilla
trajectory and select for marginal coverage. File duration and combat density in
the recorded Slippi postframes do not establish useful vanilla execution: the
first complete calibration had seven stock losses but only five damage
increases. Keep the calibration as a regression control even when a denser
workload adds more coverage.

The first expansion freezes these sources from `erickfm/slippi-public-dataset-v3.7`
revision `c82be5f6e43f3388555cfe0cf8652580601f396d`:

| Role | Public source | Complete donor SHA-256 |
| --- | --- | --- |
| Existing development control | `FOX/batch_01/19_06_05 Fox + Falco (BF).slp` | `bd3112441fdbf4c11d8e0332a64236c4022c7f930d51c1226f8e3e30def057af` |
| New development | `FOX/batch_00/11_36_13 Fox + Falco (BF).slp` | `1747654c47bbafd72b7644822d2bed686cbfc2cfe16d43a050f4fb7011f4103f` |
| Additional development candidate | `FOX/batch_00/12_37_37 Fox + Falco (BF).slp` | `302be92c000e4b09109648592be4578ec82bd42be88ff6fda609b279c45c488b` |
| Held-out evaluation | `FOX/batch_01/19_03_39 Fox + Falco (BF).slp` | `43697ca60cf380e1d76251216923b7946de5c39f369497d10c164371937b0ff1` |

All four are processed-input donors and require the explicit
`dolphin-pipe-processed-v2` policy. The held-out donor's global Frozen Stadium
flag does not change its actual stage ID, Battlefield (31). No recorded
modification is enabled in the new vanilla reference; this remains a derived
controller workload, not reproduction of the recorded modded game.

## Capture and acceptance

Discover unknown match length from the original elimination exit and its final
source draw, with the available input count as a hard cap. Every consumed
four-port PAD vector must match the immutable full plan prefix. A source-only
discovery is not an accepted reference. Cap exhaustion is explicit and cannot
be called completion. Do not guess the endpoint from the port, edit capture
headers afterward or truncate a divergence away.

After discovery, export that prefix from the complete parsed donor, retaining
the complete donor hash. Capture it twice in fresh owned Dolphin processes
using the unchanged strict fixed-count reference format. Require repeatability,
exact intended inputs, the source draw audit and original match completion.
Reference CPU, disc, Dolphin, checkpoint, external save data, controller policy
and collector identities remain pinned. Reuse immutable references during port
iteration; recapture only when their execution/input contract changes.

For each selected workload, compare the visible source-drawn port trace against
the reference pair with exact declared fields and float bits. Use headless
comparisons as fast diagnostics, retaining their declared drawing exclusion.
Require the original ending and successful source teardown. Then run visible Release
performance separately, with ordinary audio and no state serialization, first
after clearing the origin and again after a full warm application reload. Keep
the existing hard gates, including zero callbacks/intervals over 33.3 ms, live
pipeline creation, audio underruns and resumes. Retain preparation and memory
measurements separately. See the [capture procedure](RETAIL_REPLAY_CAPTURE.md)
and [performance playbook](PERFORMANCE_AND_ACCURACY.md).

Diagnose the first exact-state divergence against the pinned retail operations;
fix shared source boundaries instead of correcting replay state. Review any new
portable pipeline descriptors before adding them to the startup seed. A
discovery run or passing average frame time is not a performance pass. Preserve
raw failures alongside final results.

For a crash before the required end/teardown record, use
`scripts/diagnose_port_replay.py REFERENCE_A REFERENCE_B PORT --cpu JITARM64`.
It verifies the complete reference pair and validates the actual contiguous port
prefix, then reports its first input, PAD, fighter, RNG or clock mismatch.
Its `incomplete_capture_diagnostic` result never establishes completion or
equivalence. Preserve the raw crash and do not append a synthetic end record.
Complete captures still go through `scripts/compare_port_replay.py` and the
visible browser acceptance checks.

The frozen build inventory is `BUILD_ARTIFACTS` in
`tools/browser_replay_validation.py`. Include the imported `.mjs` asset, disc,
input and audio modules as well as the Wasm, loader, HTML and pipeline seed.
Changing any of these invalidates the current build identity. Older six-artifact
ledgers retain their historical scope; new evidence uses the complete inventory.

Coverage reports describe observed motions, transitions and events, with source
enum identities. They do not prove branch coverage, correct outcomes outside
the compared fields, pixels, emitted PCM, GPU timing or physical input latency.
A scoped replay pass does not by itself admit a fighter or stage as complete.
Stage diversity and meaningful interactions must grow alongside character
coverage. Keep game images, donors, captures and generated recipes local and
ignored; track the tools, source identities, process and concise result ledger.

## Drawing can affect later gameplay

The 3,719-tick `12_37_37` development trajectory exposes a headless limitation.
At tick 2372, retail Fox takes one point of offscreen damage and consumes the
associated RNG; the headless port does neither. Original
`ifMagnify_802FBBDC` computes `is_offscreen` during the camera draw callback,
and `Fighter_procUpdate` later consults `ifMagnify_802FC998` for damage. The
headless trace declares drawing excluded and never runs that camera callback.
The visible source-drawn browser trace matches every declared field for the
entire trajectory, including that damage, RNG and the original ending.

Retain the native red and its first divergence. Do not synthesize magnifier
flags, inject reference state or remove damage/RNG from comparison. A draw
audit showing no immediate changes to the declared fields does not prove that
drawing has no effect on later ticks through hidden source state. Browser
callback grouping and retail draw-index gaps must remain visible evidence;
this passing trajectory does not establish cadence independence for all games.

## Donor setup affects useful coverage

The ordinary vanilla checkpoint starts Battlefield slots 0/1 at center/top:
`[0, 8]` and `[0, 62.4]`. Its player spawn bytes are `-1`; original
`gm_16AE.c` resolves that default to the player slot. The baseline and
`11_36_13` Slippi donors instead start on the side platforms at
`[-38.8, 35.2]` and `[38.8, 35.2]`. Those coordinates match the
[pinned Slippi NeutralSpawn table](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/External/NeutralSpawn/NeutralSpawn.asm#L289-L300),
which the [netplay manifest injects](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/netplay.json#L134-L138).
This is a coordinate match and an inference about the recording environment;
the files do not establish every active modification.

Keep the vanilla checkpoint unchanged. A donor's long, active recording can
become a short, sparse vanilla trajectory under a different initial setup.
Screen future candidates cheaply using headers and explicit input tags, such
as early up-special intent, before expensive reference runs. Freeze source
membership before executing them, and select coverage from the resulting
vanilla states. Never transplant donor positions to make a workload denser.

## Reusable commands

Use `scripts/capture_retail_replay.py --until-match-end --frames CAP
--input-plan FULL_PLAN` with the same pinned disc, checkpoint and provenance
arguments as a normal capture. Discovery enables the source draw observer and
writes a separate `melee-web-retail-match-discovery` artifact. A successful
runner reports `discovered`; its `frames_actual` is the prefix length for two
normal captures with `--draw-audit --require-match-complete`. A cap-exhausted
run returns failure and retains its diagnostic artifact. Fixed v2 captures and
MWRC remain unchanged, and their loaders reject discovery artifacts.

For construction-PAD timing, use the runner's two-pass bootstrap CLI: run a
full-plan capture with `--input-bootstrap-calibrate`, then run the same fixed
plan and pinned setup with `--input-bootstrap PATH`. The sidecar binds the
source/plan hashes, setup snapshot and external GC state, runtime configuration
and collector identity; the resulting capture must still pass the v2 semantic PAD
configuration/history comparison at entry, initial state and every scheduler
observation. This is reusable input/bootstrap calibration evidence, separate
from gameplay equivalence or source-completion evidence. A preflight may report
the construction `PADRead` count, but that does not validate a production
bootstrap until both passes and their bindings have been checked.

Measure the resulting reference captures with their exact prefix plans:

```sh
python3 scripts/analyze_replay_coverage.py --cpu JITARM64 \
  --capture work/development-a.jsonl --input-plan work/development-plan.json \
  --capture work/development-b.jsonl --input-plan work/development-b-plan.json \
  --select 2 --output work/development-coverage.json
```

Review portable pipeline discoveries from a fresh-origin cache export only
after preserving its database, adjacent WAL and provenance manifest:

```sh
python3 scripts/review_pipeline_cache.py \
  --base web/initial_pipeline_cache.db.gz.b64 \
  --candidate work/replay-corpus/pipeline-review/candidate-fresh-origin.db \
  --provenance work/replay-corpus/pipeline-review/candidate-fresh-origin.provenance.json \
  --report work/replay-corpus/pipeline-review/review.json \
  --output work/replay-corpus/pipeline-review/reviewed-seed.db \
  --expected-base-pipelines 440
```

The reviewer copies the DB and live WAL before opening SQLite, binds both
SHA-256 values to the manifest, rejects unknown or unreviewed descriptor
types, and appends only new reviewed type-1 rows to an immutable seed. Dawn
driver-cache data is excluded by provenance and is never merged.

The report verifies each capture with the strict reference loader, binds the
plan by its byte hash, checks every actual four-port input and retains the full
donor hash. Motion labels come from pinned source enums; internal FighterKind
and external CSS character IDs remain separate. Marginal selection uses only
development observations, with source/capture hashes as stable tie breakers.
After freezing the build, `--held-out` and `--held-out-input-plan` add a
separate evaluation summary. Source-hash overlap is rejected even when the two
plans use different prefixes. This report does not itself admit gold content.

## First development expansion: observed results

The development set contains 8,670 ticks: the 3,122-tick calibration plus two
new complete executions. Both new reference pairs repeat exactly, verify all
intended four-port PAD inputs, pass the source draw audit and reach the original
elimination ending. The current collector also reproduces the complete frozen
3,122-tick reference in fixed-count mode after adding discovery.

The first new game exposed an exact knockback divergence at tick 1424:
Fox's horizontal knockback was `3f353f7c` instead of retail `3f353f7d`.
The pinned DOL uses three scalar fused multiply-add boundaries and a specific
intermediate multiplication order. A shared `ftColl_CalcKnockback` now preserves
those operations across fighter, throw, item and environmental callers.
The original failure and a read-only retail instruction/register diagnostic
are retained. A minimal Wasm regression compiles the production patched body
and includes an explicitly unfused negative control. The rebuilt native port
matches all 1,829 new ticks and the 3,122-tick calibration; the second new
game's headless limitation is adjudicated above, with its red retained.

Both new visible traces match every declared field on the frozen Release build
through original completion and teardown. Separate visible cold/warm runs pass
all hard timing, pipeline, focus, resume and audio-queue gates:

| Development donor | Ticks | Worst native callback, cold/warm | Worst browser interval, cold/warm | Match preparation, cold/warm |
| --- | ---: | ---: | ---: | ---: |
| `11_36_13` | 1,829 | 8.475 / 7.260 ms | 20.085 / 20.135 ms | 204.645 / 202.795 ms |
| `12_37_37` | 3,719 | 12.020 / 11.000 ms | 23.015 / 21.095 ms | 201.255 / 203.280 ms |

Configuration: Apple M4, macOS 26.6.2, visible Chromium 152, 640×480 framebuffer,
DPR 2, AC power, ordinary audio. Cold means the application origin was cleared;
driver caches were uncontrolled. The shorter game had no live heap growth.
The longer game grew the Wasm heap by 125,370,368 / 250,019,840 bytes in the
cold/warm runs. These are passing timing observations, not bounded-memory or
zero-allocation claims. Profile allocation ownership and retirement on this
retained workload before treating long-session memory scaling as solved.

The reviewed portable seed adds five type-1 descriptors, now 432 pipelines plus
the existing type-0 entry. All 428 original full rows are preserved, with no
payload conflicts or driver-specific Dawn cache data. The database is 1,806,336
bytes, SHA-256
`ff4349120ef3b1428cd72fbbefd2ff413d7e2208d75ad946c0113432264d1ade`.
The final Wasm SHA-256 is
`b923a1288f0d562cb76d836733f7b056b38649638279ef487d5a1246bf2b979b`.

Ignored local evidence is under `work/replay-corpus/`: discovery and fixed
captures, exact plans and MWRC recipes, raw failures, read-only diagnostics,
content-addressed browser exports, pipeline review, final machine/build profile
and immutable development-build freeze. The independent browser evidence joins
are `dev-11_36_13-browser-final.json` (SHA-256
`cf03476d451f60215f4e36fb00a4fc5bb4d153b8973eacc4c9f5634e75b90018`)
and `dev-12_37_37-browser-final.json` (SHA-256
`554ef3fdb6f3052498448a2ecfcecc11a4bb0c9ea99b7135c46719d73daa4c5c`).

Marginal selection orders `12_37_37`, the calibration, then `11_36_13`, adding
465, 166 and 37 features respectively: 668 distinct observed features. Together
they contain 18 stock losses and ten damage increases, but no up-special.
All three share Fox/Falco and Battlefield. This is a small regression cohort,
not broad fighter/stage admission. The next development candidates must add
meaningful interactions, up-special and a different stage/fighter pairing;
input duration alone is not a selection criterion.

## First held-out evaluation

The reserved `19_03_39` source was first executed after freezing the development
runtime and seed. The immutable freeze is SHA-256
`c3d877e2e5d4b085a6b3c257cb42d11de0a1c3b4453b023a227ac355519b1496`.
Source discovery finds the original ending at 2,452 ticks. Two fresh fixed-count
references repeat exactly and pass input, draw and completion checks. Both the
headless and visible source-drawn port match every declared field through that
ending, without changing the frozen runtime or seed.

Visible cold/warm Release runs also pass every hard gate, with no live pipeline
creation, timing resumes or audio-queue failures. Worst native callbacks are
8.480 / 8.070 ms, browser intervals 20.810 / 20.600 ms, and match preparation
204.145 / 186.735 ms. Live Wasm heap growth is 102,170,624 / 104,857,600 bytes;
memory scaling remains open. The configuration and cache definitions are the
same as the development runs above.

The reference A/B hashes are
`cc1abee7031563ece3327f8935da1fe8e8688b80d101071d7bbe6a40a3bb0187` and
`ee9f5996f181c7935535dd5c2ad6249794e13c7690158a429f2c56673d8a7147`.
The visible trace hash is
`7ed1d7f9c934722a001fcfcc921f17a6654e4297f0f5f443df6b3d930cce9f99`;
the independent `heldout-19_03_39-browser-final.json` evidence join is
`b5f833e4e6bea0781c194a594bbb181b45db2a4aaccc16edbdd82cd0eb0657a6`.

Its 365 observed coverage features include 59 absent from development. It has
six stock losses and five respawns, but zero damage increases and no up-special.
This is a successful held-out movement/lifecycle check, with limited combat
coverage. It does not establish broad distribution coverage or gold/content
admission. The four distinct workloads total 11,122 source ticks.

Validation also passed the full 453-test regression suite, the updated 12-test
coverage suite (including two additional cases), and the rebuilt Release combat
trace. Release menu/content lifecycle targets were rebuilt before their full
suite checks. The upstream Melee checkout remains pristine. All owned Dolphin,
debugger, browser validation tabs and the local evidence server were closed.

Next: select denser inputs and a different fighter/stage pairing, establish its
ordinary source checkpoint, and repeat these gates. A Marth/Falco Yoshi's Story
setup bundle is preparation only; its copied base savestate is not a completed
Marth checkpoint or evidence. Preserve this cohort as regressions, profile the
observed heap growth, and reserve fresh source hashes before subsequent held-out
evaluations that follow runtime changes informed by evaluation data.

## Second expansion: frozen inputs and memory baseline

Before runtime tuning or new original-game execution, a bounded screen of 12
complete sources at the same pinned dataset revision reserved two fresh
held-out donors:

| Public source | Complete donor SHA-256 |
| --- | --- |
| `FOX/batch_01/19_32_48 Fox + Falco (FD).slp` | `0376f27fe92a224a1c6a972493292562f0b07391a7265dea480bc13806b6b433` |
| `MARTH/batch_00/19_11_16 Marth + Falco (YS).slp` | `0e073460262b77369111e20098644531b9e718fbc6a21608097cb2d9bcabe0e9` |

Screening used headers, complete input availability and pre-frame controller
intent, including early B+up-like inputs. It did not score donor post-frame
combat. The initial development executions are Fox/Falco FD `16_38_42`
(`18e6ef1e426c5b4a3040c121d6e5aa3aeaed50e9028bd5b9deac09131f2ae9d8`),
Fox/Falco FD `21_06_57 [NELL]`
(`03075ddd0e7b927a3aa0905205874e75ce16ab2764b5ac8cc8a69b25fdf63012`),
Marth/Falco YS `15_07_17`
(`12f970373838985577ed187bb2a6a0446f31cce82c6c398636de68112d4f195b`),
and Marth/Falco YS `20_39_26`
(`0323db5f3de35c79b8b91aba6dbc9ffb7f22d19b702879abe04d4449227dbdba`).
Selection is a workload hypothesis; only the new vanilla trajectories establish
observed combat or recovery coverage. The ignored source manifest and full
input plans are under `work/replay-expansion/`.

Replay reports now sample Wasm capacity and the pinned dlmalloc allocator's
live/free/top-free bytes before preparation, after preparation, before teardown
and after teardown. These lifecycle samples stay outside the live source clock;
`mallinfo` walks allocator metadata and must not run every tick. The existing
`wasmHeapGrowthBytes` metric still measures capacity during live execution, not
live allocations. Post-teardown intentionally includes imported files and
decoded archive/audio/renderer caches retained by the application.

Three complete 3,719-tick development replays in one visible application, without
reload, cache clear or disc reimport between matches, returned post-teardown live
allocations of 106,765,288 / 106,765,144 / 106,765,136 bytes. Wasm capacity ended
at 922,157,056 / 922,157,056 / 1,047,986,176 bytes. All three passed the existing
timing gates, but the third still grew capacity by 120 MiB. This is evidence of
stable live ownership for the repeated workload, with unresolved transient
allocation or fragmentation costs; it is not a general bounded-memory claim.
These are repeated-session diagnostics, not cold/warm acceptance runs.
The raw reports and artifact identities are retained in
`work/replay-expansion/memory-browser/session-before-fix.json`.

### Bounded browser staging

The pinned browser WebGPU bridge implemented `GetMappedRange` by allocating
Wasm memory for mapped ranges and copying them back on unmap. Aurora mapped five
ranges totaling 63 MiB per frame, regardless of their used lengths. Repeated
mapping allocations explain the capacity/fragmentation problem without a
growing retained live allocation in this workload.

The browser path now owns two persistent 63 MiB CPU shadows and uploads their
used prefixes with `Queue.WriteBuffer`. Original command-buffer copies and draw
order remain intact. Two GPU staging leases enforce completion backpressure;
retired-generation callbacks cannot release new leases, and completion failure
fails explicitly. Ordinary per-frame mapping and the temporary 192 MiB match
reserve are removed. Native rendering and original Melee source are unchanged.

On the same visible Release configuration, three further complete 3,719-tick
replays in one application passed every hard timing gate. Wasm capacity stayed
at **334,102,528 bytes** after preparation and through all three teardowns, with
zero live-gameplay growth. Post-teardown live bytes were
238,886,088 / 238,885,968 / 238,885,984, including the explicit 126 MiB CPU shadow
owner. Worst native callbacks were 10.000 / 7.320 / 7.160 ms; browser intervals
24.930 / 21.020 / 20.080 ms. Average logical staging use was approximately
1.3 MiB per callback, with first-run peak 3,115,276 bytes. This measures Wasm
allocation and submitted data, not total browser/driver/GPU memory.

The complete before/after reports and build hashes are retained in
`work/replay-expansion/memory-browser/session-after-fix.json`. These repeated
matches establish bounded reuse for this retained workload; new stage/fighter
combinations still require their own state and performance checks.

The separate visible state run matches both original-game references through
all 3,719 ticks and retains the exact prior visible trace hash
`74bfc62ecff76fbfd29e5aef223dad0540f8eb824f58f343065e660197338b17`.
The first cleared-cache run above and a fresh application with persisted cache
pass the strict joined state/completion/cold/warm gates. Warm native maximum is
7.430 ms, browser interval 20.780 ms, preparation 216.490 ms, and live heap
growth zero. Cold preparation is 250.540 ms. Both inspect empty browser error
logs; driver cache remains uncontrolled. The joined receipt
`staging-dev12-browser-final.json` is SHA-256
`a0170e7d4435a824ff95d7ef9b64995c39f615fe9d11dbdf2c511e018bd273d2`;
Wasm is `74c7c1256ae3e458f0d4e57b03666f9e18cbf694a8891fb50b770a57e5ade25c`.
This is exact declared-state and scoped timing evidence, with no pixel, PCM,
hardware-input or broad content admission claim.

## Expanded development corpus — eight-game regression gate

The v2 coverage report
`work/replay-expansion/development-coverage-v2.json` (SHA-256
`f891c3cbc6d9b2569c96c5ebbf397b8d15e5babb55cde8f311eabc0f29fb9bc9`) analyzes
the four new retail-A trajectories as development candidates and compares them
with the prior four-game cohort as historical comparison data. The coverage
file names that side `held_out`; all four are now development regressions,
not untouched evaluation games. It uses pinned
source mapping SHA-256
`7d912a27ec94d8ad34cac9b793e3bfc24b3f71b65c80f878cb38a2f400b07cbd`, retains
the complete donor identity, and reports `gold_admitted: false`.

| Workload | Complete donor SHA-256 | Stage / fighters | Ticks | Stock losses | Damage increases | Respawns | Up-special states observed |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| New FD `16_38_42` | `18e6ef1e426c5b4a3040c121d6e5aa3aeaed50e9028bd5b9deac09131f2ae9d8` | FD / Fox-Falco | 3,765 | 6 | 19 | 5 | none |
| New FD `21_06_57` | `03075ddd0e7b927a3aa0905205874e75ce16ab2764b5ac8cc8a69b25fdf63012` | FD / Fox-Falco | 3,535 | 7 | 14 | 6 | Falco: 3 |
| New YS `15_07_17` | `12f970373838985577ed187bb2a6a0446f31cce82c6c398636de68112d4f195b` | Yoshi's Story / Marth-Falco | 2,901 | 6 | 3 | 5 | Falco: 4 |
| New YS `20_39_26` | `0323db5f3de35c79b8b91aba6dbc9ffb7f22d19b702879abe04d4449227dbdba` | Yoshi's Story / Marth-Falco | 3,500 | 6 | 7 | 5 | none |
| Prior BF `19_06_05` | `bd3112441fdbf4c11d8e0332a64236c4022c7f930d51c1226f8e3e30def057af` | Battlefield / Fox-Falco | 3,122 | 7 | 5 | 6 | none |
| Prior BF `11_36_13` | `1747654c47bbafd72b7644822d2bed686cbfc2cfe16d43a050f4fb7011f4103f` | Battlefield / Fox-Falco | 1,829 | 4 | 1 | 3 | none |
| Prior BF `12_37_37` | `302be92c000e4b09109648592be4578ec82bd42be88ff6fda609b279c45c488b` | Battlefield / Fox-Falco | 3,719 | 7 | 4 | 6 | none |
| Prior BF `19_03_39` | `43697ca60cf380e1d76251216923b7946de5c39f369497d10c164371937b0ff1` | Battlefield / Fox-Falco | 2,452 | 6 | 0 | 5 | none |

The new trajectories total 13,701 ticks, 25 stock losses, 43 damage
increases and 21 respawns. The prior four-game comparison totals 11,122 ticks,
24 stock losses, ten damage increases and 20 respawns. Up-special coverage is
present in two new trajectories, both Falco observations; the counts above are
distinct pinned motion states in the observed `special:up` family. The v2
report supersedes v1's `special:ground-*` and `special:air-*` labels with
direction-only special families; ground or air posture remains in the observed
`ground_air` fields and transitions. Existing v1 reports remain unchanged.

The immutable reference artifact hashes are:

| Workload | Retail A SHA-256 | Retail B SHA-256 | MWRC recipe SHA-256 |
| --- | --- | --- | --- |
| New FD `16_38_42` | `8a0da77abe54cea68a961629a4f4a096853d11762749a34b2ce229488be6d5ea` | `995c98828a694b609934b9f1cc73c6214a6e18eb287e181dc9b2db962a4958e2` | `7b7617817588047aec256aebfd701894e61e794fc0ef80edf65746108f7a489c` |
| New FD `21_06_57` | `c4e71b0b80b161ade524105c806f66cfcc592b253499fff14892a6312543b0a6` | `637b0c881bb74ebf2ee4da236a0d97c30b47c10c341fb15e62fc1db3774431f8` | `48d1a08eb5eaa7d653860d450d4e89cccb03b1e1b15a7bf4aea64a5d38bf9a6f` |
| New YS `15_07_17` | `276bafbcf067b4fa5099f7aadc43ca21064efe36083f0fa6a4c2ce7c002b6411` | `c4b530f5279ca550a2f5f761d368a5194c04a1df8aff333ce77a81ca7c5414fc` | `3aedd1b90444bd7465a80d0671474708197e21005eb4c9080d268b212deb5d9f` |
| New YS `20_39_26` | `aba8d5f49deeacaa118f4caccebbd9cfb01deb839d7a8eb4425eb1031a7fca6b` | `174d9c27f44b740f51ee8055f8e3f3764d8cc824eb7c6b029464e7f5d66f2e14` | `38ba2ab7a95161f3fa2292f884a7ace661448030bb7a684b031c71e62bea278f` |
| Prior BF `19_06_05` | `15146ce4b3b542ee1ddf82d5345395dec551612c4f7ad82152e0d58ca061ff0d` | `56e90c852bfe0305b42e93dc3a9d71ec2a2206b9f2fde3a80483631fd6c84511` | `fa4e646ffa56aa0ad34938a4f427fa9e5600ad18a6308a165566f33e5f36e54a` |
| Prior BF `11_36_13` | `5ce2679daea4b5f44251b0f0d26ea4653d87820c0d4b8b5b05ffd23d9a8a1306` | `f57f1c67f3fe785958a31cc4d5b86e6021c9aa391f7c00412c857acb40558080` | `4f6d37f9df0638f56e33a76962734bb2863b06106d1d00f84802a04e59460541` |
| Prior BF `12_37_37` | `d268186b50ece52788b052633a43ee30c154ffaf4de45f4d722c693db01439ff` | `93c1f9f65115c25260a65ba88a0a98b10b5ab3c40f344887c12b6577dbec4e7f` | `d8e8f7d1e0ec8171642d651176ba72135a46e25462926b614e5806e2c9f50e26` |
| Prior BF `19_03_39` | `cc1abee7031563ece3327f8935da1fe8e8688b80d101071d7bbe6a40a3bb0187` | `ee9f5996f181c7935535dd5c2ad6249794e13c7690158a429f2c56673d8a7147` | `7e28fba9b221fa99d6681e315bd0b129144f899ba167beb35d9d1a37a8207f09` |

### Reference repeatability and port status

All four new A/B reference pairs independently repeat with complete input,
frame-order, provenance and semantic-state checks. This reference result is
separate from port validation. All eight visible declared-state comparisons
now pass through original endings and teardown on the final shared build.
The FD `16_38_42` damage/RNG red at tick 2,396 exposed missing original
`Ground_801BFFB0` initialization: a zero floor constrained the main camera,
causing earlier magnifier damage. Restoring the source reset before map archive
publication fixes the full 3,765-tick visible declared-state comparison against
both retail references (trace SHA-256
`c7d676a238528f7118057652ff666c9d80e8967fc840c26f8ce7322fbcac4a2f`).
The original 60-tick damage threshold is unchanged. A separate `Mtx44`
projection-buffer correction removes a 16-byte overwrite; its isolated full-game
control preserved the failing trace and did not explain the camera red.
Selected camera coordinates still differ from retail, so this gameplay result
is not pixel or camera equivalence.

Yoshi's Story `15_07_17` now matches both references through all 2,901 ticks,
including visible source drawing (trace SHA-256
`b17481b1cf3a3a7ed7993d450c6de2e9592caa38bf0e1aca185ebff6ded4c86a`).
Its tick-1,146 position red required shared pose and SDK quaternion-matrix
rounding corrections. Identical retail scalar inputs establish 1,485 exact
pose-call outputs and 135 exact quaternion matrices before the full replay.
The prior implementations differed on 833 and 111 calls respectively.
Noncausal wall-interpolation edits were removed; no tolerance, expected-state
injection or special-case replay correction was added. See the
[scalar capture procedure](RETAIL_REPLAY_CAPTURE.md#shared-pose-and-quaternion-matrix-scalar-oracles).

The updated development seed preserves all 446 prior pipelines and appends 23
validated type-1 keys from a cleared-origin YS `15_07_17` discovery run (19
preparation and four live creations). It contains 469 pipelines plus the same
shader row, SHA-256
`30a502f41112c06663b9fa8655a380e9cbeae065f4f2475e81172f5929a1003f`.
The review retains original row metadata, verifies payload version/size and
records DB/WAL provenance in `pipeline-review/ys15-review-final.json`.
The final Release build passes all eight complete visible state comparisons
(24,823 ticks), followed by 16 isolated application-cold/warm performance runs
(49,646 ticks). All hard counters are zero: callback gaps over 33.3 ms, native
callbacks over 33.3 ms, browser long tasks, timing pauses, live pipeline queue/
creation, and audio underruns/overflows. No run resumed a hitch. Worst native
callback is 15.855 ms and worst browser interval is 26.560 ms. Application-cold
preparation ranges from 184.520–238.970 ms; warm preparation is
182.910–218.160 ms. Driver caches remain uncontrolled.

All eight consecutive instrumented state runs keep Wasm capacity at 334,102,528
bytes through teardown; all 16 isolated timing runs have zero live heap growth.
The named machine is Apple M4 / Mac16,12, 32 GiB, macOS 26.6.2, visible Chromium
152, 640×480 with DPR 2. The recorded development profile starts on battery
power with Low Power Mode off; it is not continuous power telemetry. Timing runs exclude
concurrent builds, tests and Dolphin captures. These are declared-state and
scoped performance gates; pixels, PCM, physical input, hardware timing and broad
gold/content admission remain open.

`work/replay-expansion/browser-profile-psquat-final.json` freezes all 13 runtime
artifacts, including Wasm SHA-256
`9eae28251d27fea6f87e0d1246083546c0355e0a9d9fcf6fac8d848747c6ade0` and the seed
above. The instrumented state ledger is `psquat-visible-state-runs.json`; the
isolated timing ledger is `psquat-performance-runs.json`. Each joined receipt
checks both complete originals, recipe, source-drawn state, cold/warm reports,
build identity, source ending and inspected browser error logs.

| Workload | Worst native cold / warm (ms) | Joined receipt SHA-256 |
| --- | ---: | --- |
| dev-ys-15_07_17 | 12.400 / 13.000 | `ab1b561a2791f28a6a44684114c6f7dcd93f097ab5e2c428b61b0a78c51dce3c` |
| dev-ys-20_39_26 | 13.270 / 15.855 | `abc4817a0be22a56b0c494626249e66a528b3e7117424ef71c21652357f12140` |
| dev-fd-16_38_42 | 10.255 / 11.820 | `9b3468598745eb5dad5ed72131ecb132140b919b3c2211953b1b365338f4801b` |
| dev-fd-21_06_57 | 10.535 / 12.320 | `f254fe00767a5502e23bffd2353a76ab2a03bebdc3c029ed793d21a7761c967f` |
| dev-12_37_37 | 9.645 / 9.790 | `c036783ef83eb5e47a63c4fa2e97982e3b3b8a05430141553a57691974a14885` |
| baseline | 9.155 / 11.175 | `2401cd937972a77276fa39945e78177f874752b0d55e001614412fde08d9ee9a` |
| dev-11_36_13 | 7.670 / 13.085 | `2a96e56b083ad3cc4f083f303910f78d14b7c9e1dc899396a9325f4cb7abac2d` |
| heldout-19_03_39 | 10.855 / 7.885 | `a0fa6186c6ecdbfeed467f38561c31bf6e1c51a5136387257cd0ed78d8ba9e5d` |

At the development freeze, the two previously reserved donor identities were still
unexecuted:
`0376f27fe92a224a1c6a972493292562f0b07391a7265dea480bc13806b6b433`
(Fox/Falco FD) and
`0e073460262b77369111e20098644531b9e718fbc6a21608097cb2d9bcabe0e9`
(Marth/Falco Yoshi's Story). Their input-only plans and identities are in
`heldout-execution-plan.json`. Reference and port execution require an explicit
runtime freeze after the eight joined development gates pass; any tuning after
an evaluation failure moves that donor into development.

## Two untouched evaluations on the frozen expansion

Commit `01fd0f3ca421d15e5aeb375ec8e6ebd7e854fd2c` freezes the shared runtime and
469-pipeline seed after the eight development gates. The ignored freeze record
`work/replay-expansion/heldout-runtime-freeze.json` has SHA-256
`386360e3027ec31d0c4db8201ea753317be1f0132d8f6b96f3eb76a7df58aec1` and binds
the source tree, all 13 browser artifacts, selected donor/input-plan hashes and
the eight joined development receipts. Those artifacts are unchanged after
both evaluations; neither held-out game required runtime or seed tuning.

Source-driven ending discovery is followed by two fresh, fixed-length complete
retail captures for each reserved donor. Both pairs independently repeat exactly.
The visible browser state matches all 6,219 ticks, with full source drawing,
original endings and teardown. Four separate isolated cold/warm runs pass every
hard timing/resource/audio-queue gate, with zero resumes, live pipeline creation
or live heap growth. Original-reference processes are stopped before timing.

| Held-out game | Complete ticks | Native cold / warm (ms) | Preparation cold / warm (ms) | Joined receipt SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Fox/Falco FD `19_32_48` | 4,608 | 10.120 / 10.360 | 186.540 / 184.200 | `c7ffaf3c5176fbbc7a64526994c954c0fdcbcec584c147dfcea07c79e61337f9` |
| Marth/Falco YS `19_11_16` | 1,611 | 12.435 / 12.965 | 187.140 / 185.810 | `a900799b3f906b7a6c7b7d31cac8244b676f73ca71161b35315bdce97b16463f` |

| Game | Retail A SHA-256 | Retail B SHA-256 | MWRC SHA-256 | Source-drawn port SHA-256 |
| --- | --- | --- | --- | --- |
| FD `19_32_48` | `690efc3374b3ac8a19b93eb98da55803b4c8106b8f22d07352eb8f6fcabb0787` | `19630f14d5c02cf35eeb17e82048728fd80c5fd829b7941984814cc19732fe24` | `44dc466ea44dd61c70a2347209014ed4d8e53c0cf96a2cf36274673ff1b70b42` | `6e6fc534ddaccbd929030907285f8b00c206dc75a91a3f3095000757d01b3b4f` |
| YS `19_11_16` | `822b434f750fe6229e344ddd0d52a638eee7f99f7f3bd507853c9d75929fe971` | `c487797fe2a09daf123d7a23af67cbb836a0787d9ccdef7d1fa7a2a361ea9987` | `83a889c7e9b03a0ba785fcea56a81b1efaf92a06261ce7cc0ca20f5be86b08af` | `1c8f8ee44c06cadab4d32dfba0426dd63245621638fd405c97c6ad68bd69fece` |

The local `heldout-browser-runs.json` ledger preserves all six state/timing
reports; `heldout-result.json` joins their final status. The profile
`browser-profile-heldout-final.json` records the same Apple M4 / Chromium 152
configuration, now on AC power; the separate state profile allows reference
capture concurrency without claiming timing acceptance. Driver caches are
uncontrolled. All inspected browser error logs are empty. The owned browser
tab, test server and reference processes are closed after evaluation.

Together the frozen build passes ten complete games, 31,042 source-drawn
comparison ticks and 20 cold/warm performance runs totaling 62,084 ticks. These
are input-donor-derived vanilla reference trajectories and declared-field
checks, not admitted gold Slippi reproduction, full pixel/PCM equivalence,
hardware input/latency, or complete fighter/stage admission. The next cohort is
the separately reserved UCF-off development/held-out split below; it has not
been executed or used to tune this build.

## Independently verified UCF-off candidate split

The earlier 952-file sample and its approximately 0.50% one-sided UCF-off bound
describe that sample only. They are superseded for corpus-wide candidate
selection: the older sample's source identities and the pre-filter inventory
needed to explain the difference are unavailable, so the cause of the changed
observed rate remains unresolved. Do not treat the earlier zero-observation
result as a full-corpus constraint.

At pinned dataset revision
`c82be5f6e43f3388555cfe0cf8652580601f396d`, the ignored manifest
`work/replay-expansion/vanilla-candidate-split-2026-09-11/manifest.json`
(SHA-256 `db441dfb6069672a3946dc0026f0da02bdbcb1e9340b0075d72103611905748f`)
records 76 complete source identities. The pinned decoder and installed
`@slippi/slippi-js` 9.1.3 agreed on all 76 headers and Game End records; the
16 exact human P1/P2 sources passed complete parsing and explicit
`dolphin-pipe-processed-v2` export. Selection used headers, stage/matchup,
ports, normal Game End and pre-frame intent only. No runtime or reference
execution had occurred at reservation, and `gold_admitted` remains false.

| Role | Complete donor SHA-256 | Stage / matchup |
| --- | --- | --- |
| Development | `323d7ff17991fd5c47f7584531b5e50128132e0543e659d13ca0696ef5986f23` | Battlefield / Falco-Fox |
| Development | `4683290e23b85b189f6157871b46690a67d5456f304e75b4dca2cd0cf0a0d5c8` | Yoshi's Story / Marth-Marth |
| Development | `59cee05bb6d0f37e4cafd7100b0ffb73ea3c2886412328ded5b661aa3fd8afba` | Final Destination / Fox-Fox |
| Development | `7e5a8d15187dabb5e9a658fe025e2cb301c968a2c1f6a7bcad440675e8dc4002` | Yoshi's Story / Falco-Mario |
| Development | `8075c5d52d965088e0ebe36b5974e29ad399206099e770178dd344e936b316cc` | Dream Land / Falco-Marth |
| Development | `a822bb8d76fad6ec60fa034eef8b1e560edb07bad5cb90ac49ac8ce82c973f9a` | Dream Land / Fox-Marth |
| Development | `dca20df0227367841f09e6a53b4557d3ffcbd6c3028c00d1e7b7a5b5300f5d1d` | Yoshi's Story / Falco-Marth |
| Development | `e2a3baac277f9ef20b59efad37bf90b5fd456fa0e595e99f2a8a5ecfd542819c` | Dream Land / Falco-Falco |
| Held-out, reserved | `277c7e0e2f42ceb42435b16e639fb97b554fbf9a1574059e809967179aec594a` | Battlefield / Marth-Marth |
| Held-out, reserved | `feeb4d6d52ccc2f25fd0eabf75794c5aebd28ee2b1bfc17e2d74f316784924cd` | Final Destination / Falco-Falco |

These legacy recordings do not provide complete modern raw PAD fields. Their
plans therefore use the explicit processed-v2 policy, which derives axis bytes
from bounded processed axes and preserves physical buttons and invertible
trigger bytes. This is a derived input workload, not recovered hardware input
or UCF-state equivalence. The plans retain donor identity and inputs but do not
transplant donor fighter positions; initial retail spawn setup remains a
separate validation boundary.

### Timed-cohort preparation in progress

The frozen development execution plan is
`work/ucf-off-cohort/development-execution-plan-v3.json`, SHA-256
`4d6d25ea2f57bcf2c1625dc7602306fd26a140b2c43165750a9eb6dc5b6407e2`.
It binds the existing source/input hashes and each exact stage, character,
costume, stock count, timer, pause, item and rumble setting. All eight development
headers have an eight-minute countdown; the original menu defaults cannot be
substituted. The independently checked setup inventory is
`work/ucf-off-cohort/development-setup-audit.json`, SHA-256
`f277c36c7079849a4c70e5893fc99ec4b2f229f042fa83204a15d71e479a9d75`.
The historical reservation manifest remains unchanged and both holdouts remain
unexecuted.

The timer candidate's first untimed regression matches all declared fields
through 3,122 ticks with visible source drawing. Its local comparison is
`work/ucf-off-cohort/baseline-bf-browser-comparison.json`; rendered trace SHA-256
is `af184be8e15b061cf60930a8176373f36af5b091a2f68a17150df608edb16414`.
The native timer lifecycle also passes countdown, pause/resume, timeout and a
second construction. The first timed comparison and isolated cold/warm result
are recorded below; the remaining development cohort is still pending.

Before executing a named development workload, bind its complete retail
capture to the frozen setup and exact input-prefix identity:

```sh
python3 scripts/check_retail_setup.py \
  --plan work/ucf-off-cohort/development-execution-plan-v3.json \
  --name ucf-dev-323d7ff1 --capture /path/to/complete-retail-capture.jsonl \
  --repo-root . --cpu JITARM64
```

The gate checks the full donor source/input-plan hashes, the exact consumed
prefix and its capture hash, and the declared donor `StartMeleeData` fields
before any runtime result is considered. It also rejects unsupported ordinary-VS
profile flags, camera scale and callback pointers; this is not a claim that the
manifest freezes every opaque setup byte.

### First bounded UCF-off development donor

The first executed development donor is
`323d7ff17991fd5c47f7584531b5e50128132e0543e659d13ca0696ef5986f23`
(`Battlefield / Fox-Falco`). Its frozen plan consumed exactly 5,772 ticks. The
raw fixed captures and prototype joins are retained in
`work/ucf-off-cohort/ucf-dev-323d7ff1/`: `prototype-fixed-a.jsonl`,
`prototype-fixed-b.jsonl`, `prototype-browser-comparison.json` and
`prototype-timer-comparison.json`. The prototype joins report exact declared
state/PAD/RNG/match-clock and timer agreement for all 5,772 ticks. The
production two-pass bootstrap is now separately validated: `production-a.jsonl`
(SHA-256 `0a5c222379d1bbe577bd512fb4d5f0ce20b126a260e429805675dd898cc5e05a`)
matches that entire repeated trajectory, its exact setup gate passes, and its
timer sidecar is byte-identical to both references. `bootstrap-v4.json` binds the
20 construction PAD reads, complete input plan and canonical runtime profile;
only the verified run-owned debugger socket token is excluded from that profile.
`production-collector-calibration.json` and `production-setup-gate.json` retain
these separate checks.

The donor ends at its last KO (P1 stocks 0, P2 stocks 3), while the original
source ending was not observed before the available plan ended. Its discovery
record is therefore `cap_exhausted` with `complete_match: false`, no exit or
match-end observation, and no full-match or gold admission. The preflight
construction count was 20. The current protocol does not extend a frozen donor
with neutral PAD vectors, rewrite its end record, or synthesize source ending
evidence; the bounded result remains a workload observation.

The initial timed-stock candidate passes the independent state/timer/cold/warm join in
`work/ucf-off-cohort/ucf-dev-323d7ff1/browser-seed473-final.json`. The final
source-drawn state and timer traces are byte-identical to the pre-seed traces.
Both 5,772-tick performance runs have zero callbacks over 33.3 ms, live pipeline
creation, timing resumes, browser long tasks, heap growth and audio queue faults.
The source-derived coverage report (`coverage.json` beside the captures) records
81 damage increases, five stock losses, four respawns, and execution of all four
special families. Its 589 observed feature labels are total coverage for this
workload, not a claim of 589 additions to the previous corpus.

| Profile | Worst native callback | Worst browser interval | Match preparation |
| --- | ---: | ---: | ---: |
| Cleared application origin | 15.920 ms | 25.885 ms | 201.800 ms |
| Full application warm reload | 11.870 ms | 26.150 ms | 201.330 ms |

Configuration: Apple M4, macOS 26.6.2, visible Chromium 152, 640×480 framebuffer,
DPR 2, battery power with low-power mode off. Driver caches were uncontrolled.
There were no concurrent tests, builds or Dolphin captures during timing runs.
That build profile is `work/ucf-off-cohort/browser-profile-timer-seed473.json`;
Wasm SHA-256 is
`a1c1e7c8e8dab852914c32bb423ab381cfe8678088a71aa83386f9795cb60693`.

The workload discovered three live pipeline descriptors and one preparation
descriptor. The append-only review in `work/ucf-off-cohort/timed-pipeline-merge.json`
preserves every prior row and adds four portable descriptors, bringing the seed
to 473 pipelines plus its existing shader record. Database SHA-256:
`197d7d9eb58d60e3eaba40f4b1742bd730eda1956c8bcc43a2c119a4871fbb39`.
The renderer seed changed; the game simulation did not. This remains a
development candidate and is not the reserved holdout build freeze.


### Timed Marth/Yoshi’s and Fox/Final Destination development

The next two reserved development donors preserve their exact eight-minute
stock setup, costumes, pause and rumble policy. Their input plans are unchanged
prefixes of the frozen donors, ending when the original source ends the match:
Marth/Marth on Yoshi’s Story consumes 6,525 of 11,209 available ticks, and Fox/Fox
on Final Destination consumes 7,481 of 10,965. Each has independently repeated
original captures, the original ending/final draw, and a passing exact setup gate.
These are original vanilla trajectories driven by derived SLP input, not a claim
that processed SLP samples recover the original raw controller stream.

The Marth trajectory exposed three shared numerical boundaries: paired-single
matrix/vector lane order at tick 644, Dolphin Slash velocity rotation at tick
1,971, and linear spline interpolation feeding Randall at tick 5,918. Read-only
original scalar probes and the pinned DOL instruction order identified the
rounding differences. The port now preserves those fused-operation boundaries.
Original moving-platform vertex/bounds and segment-remap fused operations were
also restored after instruction verification; those changes alone did not close
the observed reds. No state field or tolerance was changed. The final headless and source-drawn
Marth traces match both original captures through all 6,525 ticks. The Fox
source-drawn trace also matches all 7,481 ticks; its retained headless damage/RNG
red at tick 6,245 is absent with original drawing, consistent with the existing
magnifier dependency. Both timer sidecars match exactly. Performance results
for the final candidate are recorded separately below.

Evidence is under `work/ucf-off-cohort/ucf-dev-4683290e/` (`retail-a.jsonl`,
`retail-b.jsonl`, `paired.mwrc`, `headless-spline-comparison.json`) and
`work/ucf-off-cohort/ucf-dev-59cee05b/` (`canonical-retail-a.jsonl`,
`canonical-retail-b.jsonl`, `canonical-repeatability.json`,
`canonical-setup-gate.json`, `paired-canonical.mwrc`). The initial Fox pair used
noncanonical JSON whitespace for its prefix and was rejected by the setup hash
gate. It remains diagnostic evidence; the canonical pair was independently
recaptured. Serialize a prefix with sorted keys, compact separators and one final
newline, and validate that exact identity before launching a capture.

The remaining five development donors and both fresh holdouts are unexecuted.
The candidate is not frozen, and there is no new gold or content admission.


The final candidate's 510-test suite passes (`full-suite-final-math.log`,
SHA-256 `23cd2f2d59c42fc8f42c838df1bd5ab9c506d37873b642397abfbd4471227c66`).
The rebuilt timer and admitted-content lifecycle targets are exercised by that
suite. Shared matrix/vector tests use nine original scalar vectors, verify both
SDK aliases and in-place operation, and retain the old-order failing control.
The spline test reads the owned ignored `GrSt.dat`, verifies its hash and actual
descriptor, then checks nine original samples through the compiled source; the
unfused control fails. No extracted stage asset table is stored in the tests.


The current numerical candidate also passes all ten previously executed games
with original source drawing: 31,042 exact ticks, including the known
magnifier-dependent case. `work/ucf-off-cohort/final-prior-visible-regression.json`
(SHA-256 `3ddf74a4111f6cc86625f1411e4d9cee333ebd04064903e76cfd81b1c4e41c3b`)
binds every trace/report. All ten consecutive teardowns retained Wasm capacity
334,102,528 bytes. This regression is state evidence; its instrumented timing
must not be substituted for isolated performance runs. The two new reserved
holdouts remain unexecuted.


### Final timed-cohort candidate performance gate

The same final build passes the independent state/timer and cold/warm joins for
all three executed donors. The two complete original endings are also joined for
Marth/Yoshi’s and Fox/Final Destination; Battlefield remains the explicitly bounded
5,772-tick recording. All six isolated performance runs pass with zero callbacks
over 33.3 ms, browser long tasks, timing resumes, live pipeline creation, live
heap growth, preparation pauses and audio queue faults. No tests, builds,
Dolphin captures or state observers ran during timing measurement.

| Donor / stage | Ticks per run | Worst native cold / warm | Worst browser cold / warm | Preparation cold / warm |
| --- | ---: | ---: | ---: | ---: |
| Fox/Falco / Battlefield | 5,772 | 13.495 / 15.535 ms | 23.820 / 23.580 ms | 202.575 / 201.155 ms |
| Marth/Marth / Yoshi’s Story | 6,525 | 16.710 / 23.755 ms | 25.560 / 27.205 ms | 186.120 / 188.030 ms |
| Fox/Fox / Final Destination | 7,481 | 10.130 / 12.815 ms | 21.470 / 23.920 ms | 167.885 / 166.245 ms |

This adds 19,778 exact visible timed ticks and 39,556 performance ticks on the
named configuration. With the prior visible regression, this candidate has
50,820 exact source-drawn ticks across twelve complete games and one bounded
recording. It does not retroactively revalidate the prior build’s twenty timing
runs. The current six timing runs use the same Apple M4 / macOS 26.6.2 / visible
Chromium 152 / 640×480 / DPR 2 configuration, battery power and low-power mode
off. Each cold run clears the application origin’s render cache; each warm run
recreates the entire application heap. Driver caches remain uncontrolled.

The final profile is `work/ucf-off-cohort/browser-profile-timed-math-final.json`.
Wasm SHA-256: `f24f54078a03e9ace694d77ac2c1c9e3f6b7c03d2d7dfb095858cb9d54e25aab`.
The reviewed 473-pipeline seed is unchanged by the numerical corrections.
The complete machine-readable ledger is
`work/ucf-off-cohort/final-validation-ledger.json`, SHA-256
`279fa2df556939062b0258777945437d1a7ff8f229003c230e89f316b7e9df0e`. Individual joins are in
`work/ucf-off-cohort/final-browser/`:

| Join | SHA-256 |
| --- | --- |
| `bf-joined.json` | `47350f70bedb6c7d166c55e75bd0c27c00c3fb036e2d631ad35adcf83c1c7af3` |
| `marth-joined.json` | `0fd0b7a9c47eeaaa01d7abab8c4b0226472fc4d1ece50415ea63dfae6d80ca83` |
| `fox-joined.json` | `89b0a40cc152248fbecf185a5a4357fe2812343fdf003f6b33202487d9e4193b` |

All original capture processes exited, and the owned test browser tab and port
8791 evidence server were closed after validation. Other existing servers were
left alone. Next: execute the remaining five reserved development donors,
resolve their observed gaps, then freeze the candidate before opening the two
fresh holdouts. Broad gold, pixel/PCM, physical-input and content admission
remain open.
