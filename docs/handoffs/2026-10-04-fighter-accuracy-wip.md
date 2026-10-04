# Fighter accuracy WIP checkpoint

**State:** Source preservation checkpoint only. This branch is not review-ready and has no PR. Do not merge or deploy.

- Branch: [`codex/wip-fighter-accuracy-checkpoint-20261004`](https://github.com/ericvanlare/melee-web/tree/codex/wip-fighter-accuracy-checkpoint-20261004)
- Base: `origin/main` at `07bcfdfa6f4531ac464eab44ebecb206daa1f0da`
- Pushed source checkpoint commit: `87c3c781c88be57a82dc86970e6ca2541a5bad12`

This WIP preserves the fighter capture/replay observer and comparator work, the Bowser callback ABI patch, the candidate Aurora untextured-draw group-2 binding and diagnostic trace, and the capture-owner output-directory fix. The latter helper's focused regression passed 1/1 before checkpointing. Python compilation and JavaScript syntax checks passed on the same source files. A Release build succeeded earlier for campaign tree `0a0761bc`, but it predates the renderer trace change and is not a build receipt for this branch. The full suite and this exact branch's build were not run.

The 26-kind coverage matrix, original v10 recording, raw traces, build receipts, failure reports, and one-off diagnostic scripts remain machine-local. See `melee-web/runs/fighter-accuracy-v11-main-07bc-20261004-20261003-225249-39c3c01a/private-manifest.json` under the verified external-storage root for their exact paths and hashes. No captures, game images, binaries, or local filesystem paths are included in this branch.

## Current evidence and open failures

The original v10 recording contains three natural four-stock CPU9 matches on Final Destination and 12 distinct selectable characters, with Results and CSS returns. It is source-only evidence; the browser replay has not reached gameplay. The prior bounded browser attempt failed in harness setup (`EEXIST` on its output directory) before Chrome launch or source-frame consumption. The corrected v6 preflight remains frozen and unrun while the shared Chrome/CDP pause diagnosis is active.

The previous renderer failure was a missing bind group at index 2 during CSS. The candidate empty-group binding and indexed-draw trace are diagnostic/candidate changes only; neither has a current-main browser result. The retained Bowser state divergence at global frame 5,239 came from an old browser artifact and is unconfirmed on current main. Captain Falcon's reported one-ULP Y mismatch and Results-to-Prize route are also unresolved on current main.

This checkpoint does not establish full-roster equivalence, or pixel, PCM, live-input, or performance equivalence. Kirby copy lifecycle/donor behavior, Ice Climbers ownership transitions, Zelda/Sheik transformations and form endings, and targeted grabs/throws, capture/release, shields, recovery, projectiles/articles, and costume/effect paths still need bounded original-game evidence and appropriate browser comparisons.

## Next step

After the shared Chrome/CDP lane is explicitly released, recheck `origin/main`, external-storage identity, port 18796, browser/Playwright identities, and every input hash in the private v6 preflight. Then run that receipt's exact `owner_command` once. Preserve the result, reduce the first actionable divergence, and only then choose a focused repair and regression. Keep the source and report identities with any resulting PR.
