# B4: first-match comparison fails at source tick 4055

**Original comparison failed**

[Issue #274](https://github.com/ericvanlare/melee-web/issues/274) remains open.
The [portable failure receipt](../evidence/v10-first-match-results-comparison-failure-v1.json)
records the single failed Results capture, strict first-divergence diagnostic,
exact source suffix, cleanup and producer identities. The separate
[#273 source-only Results receipt](../evidence/v10-first-match-results-source-v1.json)
remains valid; it does not establish browser agreement.

The headless, silent Chrome 154.0.8037.98 capture requested cursor 16291 but
stopped at cursor 16290: the recipe expected Results scene 4 while the observed
owner remained match scene 3. Its incomplete export contains 16290 session rows
and no Results witness. The final row has clock 14982 and stocks `[1,1,0,0]`;
the audited source last-match state has clock 14868 and stocks `[1,0,0,0]`.
The intended scope requires strict equality of all 15106 match states and
1184 nonmatch inputs, then a separate actual browser scene 4 entry row. Its
16384-row cap applies only to this new scope; historical caps stay unchanged.
The source audit stops before Results PAD, so that first browser Results row
would witness entry without claiming source-paired Results scalar/input equality.
The capture used 330 seconds for replay and a 390-second owner with 30 seconds
reserved for cleanup. Attributed cleanup passed. Historical #271
TIME_WAIT cleanup failure and its accepted reduction remain separate receipts.

Strict diagnosis validates the complete retained export before opening the
source. All six clock gates and both accepted stock events rejoin freshly.
The first difference is browser index 5239 / source tick 4055 / sequence 24387 /
PAD sequence 24385: RNG `2495929788` in source versus `2352289036` in browser.
Before that failure, 4055 complete match states and 1184 nonmatch inputs agree.
The diagnostic stops there, at a fresh 169638875-byte / 24388-record prefix.
It does not consume the source Results terminal or confer prefix acceptance.

The separately authorized 2958252-byte / 365-record suffix confirms complete
agreement at tick 4054 and additional fighter 1 differences at tick 4055:
source motion 90 / animation 180 / damage bits `41300000` versus browser
motion 17 / animation 9 / damage bits `00000000`, plus animation, input,
knockback and velocity fields. Match clock 3932, all four entity identities,
full PAD state and the other three fighters agree. The suffix uses the reviewed
prior entity-generation basis, not a fresh whole-prefix reconstruction.
Actual source enums identify kind 5 as `FTKIND_KOOPA`, motion 90 as
`ftCo_MS_DamageFlyTop` and motion 17 as `ftCo_MS_WalkFast`. These names do not
identify the hit source. RNG orbit positions alone do not identify actual draw
counts or a cause.

Capture/comparator tooling is commit `ace9f5d2b50d75ce53e35ed5086b147d33cbbe59`.
The three Release native artifacts retain producer
`d1914d7320e7559ed7e43b0eadf50d55ddd46e6d`; source, patches, dependencies and
native flags were checked for reuse. The historical 32-file inventory and the
current 34-file staged/served inventory remain distinct. Pre/post HTTP hashes
agree for all current files; that inventory does not assert every module was
loaded. Runtime data's actual loaded hash is recorded separately.

A preparation boundary violation is preserved: a private metadata builder
hashed the full 1449165376-byte CISO outside its authorization. That preparation
did not read MWRO or perform a typed comparison. The initial MWRO-read report
was corrected. Protected-path exclusion and a first-import audit guard were
then reduced synthetically and reviewed before one fresh metadata-only run,
which made zero protected payload-open attempts. This correction does not
retroactively authorize the earlier read.

The unchanged executable passed 2072 Python suite cases, including 153 skipped
and 1919 executed, with zero failures. Focused real comparison controls and
private diagnostic/suffix controls retain their explicit synthetic adapter
limits and failed preparations. Source and process identities stayed stable;
owned disposable scratch was removed and evidence retained.

The next experiment must reduce the fighter 1 transition and RNG ordering at
ticks 4054/4055 before any fix or long replay. Results equality, first-match or
full-session acceptance, KO/winner cause, pixels, PCM, physical input, live
timing, performance and competitive admission remain unestablished.
