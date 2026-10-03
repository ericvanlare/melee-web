# September 21 production deployment

Latest main `dab94e2` (PR #52 Donkey re-enablement, PR #53 Captain Falcon
dive-catch admission, PR #54 repository readiness) is deployed to production as
the audited `audio-player` package:
[immutable origin](https://59cf5240.webmelee.pages.dev) and
[webmelee.gg](https://webmelee.gg), deployment
`59cf5240-7deb-4447-9d38-4435c85c519f`. The exact staged bytes were promoted
unchanged from the verified staging deployment
`3309b04e-87f1-4a1d-8f24-cf0129e00a52`. Hosted HTTP verification passed on all
four checked origins (31 resources, 5 aliases each), and both headed browser
checks (ten public-player and eleven audio/PCM cases including nonzero PCM
through CSS → SSS → Mario/Final Destination → pause/No Contest → second entry)
passed on the immutable production origin and the apex; several first attempts
hit the shipped timing-guard pause on a loaded host and passed on idle-host
retries with PCM flowing. The local 1,168-test suite passes with 74 documented
asset-dependent skips. The same-session `a8d318f2` (main `4e521f6`), the PR #51
audio release `aa3d4852` and the silent releases `bed694b0`/`516608f3` remain
rollback targets. Hosted checks do not establish full-match performance,
original pixel/PCM fidelity, physical controllers or broad gameplay admission,
and the Falcon dive-catch browser interaction remains a separate gate; see the
local deployment records under ignored `work/` and
[public deployment](../PUBLIC_DEPLOYMENT.md).
