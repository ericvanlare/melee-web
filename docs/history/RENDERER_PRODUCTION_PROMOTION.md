# Renderer functional alpha promotion — September 14, 2026

The operator accepted the preview and asked to ship the existing fix once CI
was green, with loading speed as a follow-up. The functional fix is live at
[webmelee.gg](https://webmelee.gg/) and immutable deployment
[b52e23cf.webmelee.pages.dev](https://b52e23cf.webmelee.pages.dev/).
This acceptance does not erase the startup-speedup NO-GO or the hosted route
selection failures recorded in [the original evidence](RENDERER_STARTUP_FIX.md).

PR #18 was marked ready and merged using a merge commit, with its exact head
guarded by `--match-head-commit`. PR #15 closed automatically when its commits
entered main. PRs #16/#17 and the other tasks' worktrees were not changed.

| Identity | Value |
| --- | --- |
| Checked PR head | `00f5a8c6903dde5d5d43df11bb5d4285a1e4677c` |
| Merge commit | `6ead0bbe719df7f9db5f4fac6b7b8626d494ccb9` |
| Runtime source checkpoint | `05d629adbcfbefe33a0c1880088e3e3d22dc02bc` |
| Runtime directory | `4e6fa7f58a84a6ae` |
| Wasm SHA-256 | `7e15e154fbec89f2a24de55a02a5d72c725eaff69c97f4f813aed299074624a6` |
| Production manifest SHA-256 | `694d2e2f145a6d04c3a756470aa019c1f87d06cc52288d2781318f2d985acc05` |
| Production deployment | `b52e23cf-355d-494e-98e6-ca7bc29506c9` |
| Previous production | `8f59ed0b-e0d9-47c8-b9b7-a57e22b71a79` |

Both exact-head browser-build jobs passed, as did both public-shell jobs.
The public-player jobs were skipped by the workflow's manual/release policy;
the actual local public build and hosted player checks supplied that boundary's
evidence. The merge tree equals the checked PR tree; no native rebuild occurred.

The accepted preview had been packaged with preview environment settings.
Production packaging removed the staging browser titles and environment labels,
added the standard host-specific noindex headers and explanatory `_redirects`
file, and retained every runtime byte. No legal text, indexing policy, DNS,
mail, audio, storage or telemetry configuration changed. The new package passed
the artifact audit, then HTTP and all ten player UI checks at
[3869f688.webmelee.pages.dev](https://3869f688.webmelee.pages.dev/) before upload.
Exactly those package bytes were uploaded to production. The immutable
production URL and apex each passed HTTP and all ten headed browser checks,
including invalid-disc recovery, original menus, pause/resume, unload/reload,
narrow layouts, fullscreen and the application network/storage boundary.
Canonical host redirects preserve paths and query strings. The apex retains
the cancelling NEL/Report-To policy.

The retained local evidence is under
`work/renderer-production-promotion-20260914/`, outside the repository and build.
It includes the initial rejected package command (wrong build-directory name)
and a default-urllib HTTP 403; corrected build-directory selection and the
existing explicitly identified release-audit client passed. Neither was a
runtime failure, and neither failed attempt was deleted.

The executed production command was:

```sh
WRANGLER_SEND_METRICS=false \
  work/pipeline-provenance-20260913-01/deploy-tools/node_modules/.bin/wrangler \
  pages deploy work/renderer-production-promotion-20260914/public-production \
  --project-name webmelee --branch main \
  --commit-hash 6ead0bbe719df7f9db5f4fac6b7b8626d494ccb9 --commit-dirty=false
```

The 508-descriptor preparation cost remains. Historical GPU stalls, CPU
divergences and the rejected hosted route selections remain open; both human
holdouts remain unopened. Exhaustive 4×4 manifest certification and capture-size
optimization remain deferred. This promotion adds no retail-equivalence,
full-content, physical-controller or performance admission claim.
