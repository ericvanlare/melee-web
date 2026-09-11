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

Measure the resulting reference captures with their exact prefix plans:

```sh
python3 scripts/analyze_replay_coverage.py --cpu JITARM64 \
  --capture work/development-a.jsonl --input-plan work/development-plan.json \
  --capture work/development-b.jsonl --input-plan work/development-b-plan.json \
  --select 2 --output work/development-coverage.json
```

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
