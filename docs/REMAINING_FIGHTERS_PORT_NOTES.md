# Remaining fighter port notes (PR #86)

## Disposition

These six selectable characters (seven source forms including Zelda/Sheik) are
an **unfinished development candidate, not admitted or fully playable**. Source
identity/content wiring and several bounded native actions are present. The
requested headless original captures completed, but the port has not completed
either match, returned through CSS, or produced a supported original-versus-port
comparison. The current hard blockers are the rendered A/B runtime failures,
Kirby's copied-ability path, and unresolved Ice Climbers partner lifecycle.

Evidence labels below are scoped per the
[performance/accuracy playbook](PERFORMANCE_AND_ACCURACY.md). Raw disc data,
extracts, captures, screenshots, and run logs remain local and ignored under
`work/` / `assets-local/`; this note contains no personal input paths.

## Character action checklist

Construction results refer to rebuilt real-asset native source traces on Final
Destination that reached source ticks and their stated lifecycle boundary.
Targeted action runs select costume zero; only the Kirby-vs-Fox base trace names
an all-six-costume sweep here. These do not establish browser playability or
retail equivalence.

| Character | Content/construction | Targeted action coverage | Still unverified / failed |
| --- | --- | --- | --- |
| Game & Watch | **Native traced** in the mixed-content lifecycle target. | Chef reaches the source motion and creates/destroys the Chef article. | Other specials, normals, defense, recovery, hit/KO, and browser match. |
| Kirby | **Native traced** against Fox over all six costumes with repeated construction/teardown. | **Failed** while attempting the required Game & Watch donor path: execution reaches `ftKb_SpecialNDrink_Anim` then aborts in `ftParts_8007487C` / `HSD_DObjSetFlags` while setting copied-part visibility. The source-root `FtPartsDesc.model_num` conversion gets past the earlier count assertion but does not resolve this crash. | Copy acquisition, copied move, loss, replacement, and teardown are not passed. Only Game & Watch donor was attempted; Mario, Samus, and the remaining donor families are unverified. Kirby copy ownership is a required integration path, not an optional roster add-on. |
| Ice Climbers (Popo/Nana) | **Native traced** as two separate source entities; common lifecycle and reconstruction run. | Belay source up-special is observed; partner distance increases from 10 to 91.2236 units. Nana enters a source death motion against a source CPU9 opponent (maximum observed partner damage 63.3). Popo retains its entity and stock through a 180-tick follow-up. | Nana remains present after those 180 ticks (motion 11); removal/rejoin/respawn semantics are unresolved. Other specials and subsequent partner lifecycle are unverified. |
| Samus | **Native traced** in the mixed-content lifecycle target. | Down-B creates and tears down a Bomb article. | Other specials, normals, defense, recovery, hit/KO, and browser match. |
| Yoshi | **Native traced** in the mixed-content lifecycle target. | Neutral-B reaches the authored Egg Lay source motion. | Victim capture and Egg article were not observed; other specials, normals, defense, recovery, hit/KO, and browser match. |
| Zelda | **Native traced** in the mixed-content lifecycle target. | In-match down-B changes Zelda to Sheik and back; four switches were observed across both directions, twice each, retaining grounded/four-stock state. | Broader moves and browser match. Held-A startup selection is separately tested and is not used as evidence for this down-B result. |
| Sheik | **Native traced** in the mixed-content lifecycle target. | Started as Sheik; in-match down-B changed Sheik to Zelda and back, four switches observed across both directions, twice each, retaining grounded/four-stock state. | Broader moves and browser match. |

Retained native logs: `work/fighter-action-checks-v1/{gamewatch,ice-climbers,
samus,yoshi,zelda,sheik,kirby-base-vs-fox-relocation-schema,
kirby-copy-gw-v4}.log`. These are diagnostic receipts in the local checkout,
not checked-in assets or substitute for the failed action assertions.

## Original-menu CPU9 captures

Both lineups were driven through the original CSS/SSS route using headless
Dolphin and the available local profile/save/SRAM copied into isolated user
directories. The capture driver was adapted in the task's ignored work area;
retained resources were not overwritten. Reports verify the actual setup, not
just intended cursor input. Both captures use GALE01 revision 2 and Null video;
they contain source state/draw/lifecycle evidence only, not pixel, PCM,
foreground timing, controller, performance, or port-equivalence evidence.

| Run | Verified setup | Outcome |
| --- | --- | --- |
| `work/fighter-cpu9-lineup-a-reference-neutral-01` | Game & Watch, Kirby, Ice Climbers, Fox; CPU9 ×4; four stocks; stage 32 (Final Destination). | Three natural matches, Results, and return-CSS boundaries; completed status. Capture id `fighter-A-neutral-20260925-v1`, DOL SHA-256 `dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646`. |
| `work/fighter-cpu9-lineup-a-reference-neutral-03` | Same verified lineup and rules. | A second three-match natural capture completed. It is not a strict repeatability pass. |
| `work/fighter-cpu9-lineup-a-repeatability-v2` | Strict pair gate over the two completed A captures. | **Failed** with `independent whole-session streams are not source-consumed repeatable`; no recipe was emitted. Do not treat either A capture as repeatable reference evidence. |
| `work/fighter-cpu9-lineup-b-zelda-neutral-01` | Samus, Yoshi, Zelda, Falco; CPU9 ×4; four stocks; stage 32 (Final Destination). | Three natural matches, Results, and return-CSS boundaries; completed status. Capture id `fighter-B-zelda-neutral-20260925-v1`, same DOL SHA-256. |
| `work/fighter-cpu9-lineup-b-zelda-neutral-03` | Menu snapshots confirm the same B lineup/rules. | **Failed reference repeat attempt**: passive observer rejects fighter creation at source tick 252 with `fighter creation exposed an invalid source slot`; no completed run or second reference. This is an observer boundary rejection, not a failed original setup. |

The successful original captures establish that the local automated original-menu
route is available. They are not references for port correctness. Zelda and
Sheik are both covered by the in-match transformation trace above; the
successful B original lineup specifically selected Zelda.

## Rendered browser attempts and comparison scope

Installed headless Chrome ran against a real loopback HTTP server with WebGPU
rendering enabled. Screenshots and page/native diagnostics were retained. A
bounded A prefix advanced through the rendered Final Destination match to source
cursor 1633 (`work/fighter-browser-a-lineup-prefix-06`). It was intentionally
stopped at that cursor and is **not** a completed match or clean teardown.

The full attempts did not finish:

- A (`work/fighter-browser-a-lineup-prefix-04`): incomplete at source cursor
  1640 with `memory access out of bounds` in the draw path
  `it_8026EECC -> HSD_GObj_80390ED0 -> fn_800301D0`. The item kind/root cause
  was not isolated; this is not attributed to Game & Watch's Chef action.
- B (`work/fighter-browser-b-zelda-short-03`): incomplete at source cursor
  1614 (about gameplay source frame 11) with an HSD assertion at
  `jobj.h:382` in `ftCo_CatchWait_IASA` while Samus is P1. This identifies the
  failing common-action boundary; it does not establish whether the defect is
  Samus-specific or shared more broadly.

A fresh bounded A run on the final rebuilt Release runtime
(`work/fighter-browser-a-lineup-prefix-09`) captured a rendered FD frame at
source cursor 1637, then hit the same draw-path memory error by cursor 1640.
The runtime had disabled Unload after abort, so the harness's cleanup click
timed out; retain the first runtime error as the failure, not that secondary
cleanup timeout. The screenshot is an actual rendered prefix, not a completed
match or pixel comparison.

Both use `input_scheduling: per_tick`; `scheduling_equivalence` is
`not_evaluated`. The reports retain partial source state/draw data, but both
fail before full input completion, teardown, Results, or return CSS. Therefore
there is **no supported original-versus-port state comparison or first-field
divergence result** yet. Headless Null-video original captures are not pixel
references; headless Chrome screenshots confirm rendering only. These runs
also do not establish PCM, foreground timing, physical-controller acceptance,
or performance.

Retained browser evidence includes `work/fighter-browser-a-lineup-prefix-04/`,
`work/fighter-browser-a-lineup-prefix-09/` (`prefix.png` and reports), and
`work/fighter-browser-b-zelda-short-03/` (`report.json`,
`retail-browser-report.json`, `page.txt`, `failure.txt`, and `final.png`), plus
the deliberately bounded prefix folder above. The Mario baseline-control attempt
did not complete and is not a successful control.

## Next gates

1. Fix/reduce the observed common-action and rendered draw failures with focused
   reproducers; only then retry the full A/B browser matches and CSS returns.
2. Complete Kirby Game & Watch copy acquisition/use/loss/replacement/teardown,
   then target additional donor families. Do not waive copy behavior.
3. Resolve Ice Climbers partner death follow-up and entity lifecycle; expand
   targeted actions for every new fighter.
4. Obtain a valid independent B reference and repeatable A/B inputs before any
   retail comparison. Compare the existing declared source state and draw fields
   without relaxing the gate; first divergence remains unreported until a full
   supported pair exists.
5. Keep separate gates open for ordinary CSS/SSS return loops, complete
   move/action inventory, pixels, PCM, foreground timing, physical controllers,
   performance, and admission.

This PR remains one open PR. No merge or deployment is authorized by these
results.

## Integration checks

After the Kirby native/browser manifest closure was corrected, the final
`python3 -m unittest discover -s tests` run completed 1,442 tests with 88
optional skips and no failures (`work/fighter-final-suite-v3.log`). The Release
runtime target and the native `gameplay_content_match_trace` target both built.
Focused asset-manifest, fighter-asset, fighter-data, runtime-disc, web-launch,
and public-scene checks passed. These integration checks do not change the
character-specific failures and gates above.
