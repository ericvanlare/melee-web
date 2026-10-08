# Runtime-owned live input from CSS into active gameplay

**Compiled / Source identified / Browser exercised**

[Issue #283](https://github.com/ericvanlare/melee-web/issues/283) has one
independently reviewed, completed bounded local capture from original CSS
through original SSS into active Mario-versus-Mario gameplay on Final
Destination. The [portable receipt](../evidence/issue283-runtime-css-active-match-v1.json)
binds capture source and fresh ordinary Release native producer
`7db331af8df04e95be12ae4c5936aeb0c02db395` (tree
`d6bea29dc208a0ea91a89b7d664a3af27c36b5b2`, based on main
`0411e4ec37b9856eca8ca32b6e2e710c1d4cc71b`). The subsequent documentation
commit is separate and does not change this capture identity. The historical
`1792f5bf` producer/site remains preserved and is not the current native build.

Installed Chrome 154.0.8037.98 ran headless with silent speaker output on the
same-host Mac mini. Runtime-owned peers used loopback Room Worker signaling and
reliable, ordered WebRTC. Each peer acquired 518 immutable native PAD samples
through synthetic standard Gamepads and the ordinary controller manager, then
completed 520 source steps and draws. Both exported 520 ordered 64-byte
checksums, with identical streams, input ACK 517 and checksum ACK 519. Root
independently recomputed every consumed 44-byte input FNV component at delay
two from the actual samples, checking the first 304 recipe inputs and 214
neutral tail inputs byte-for-byte. The measured progress interval crossed
cursor 512 without a peer RPC or timing resume.

The checksum scenes were CSS, SSS and match in order; this run observed the
first SSS checksum at tick 154 and the first match checksum at 306. Both peers
ended the prefix at source cursor 520, phase 7. The existing native match
observer reported ready, active source frame 90, two human Mario fighters,
Final Destination stage 32 and four current and initial stocks each, with no
pause, ending or completed match. Native cursor and phase remained stable
across the observation and screenshot. Both final PNGs passed the signature,
GPU and stable-boundary guards. **Conspicuous magenta stage surfaces/outlines
and orange/dark geometry remain visible in both PNGs.** This is observed
readiness and state agreement, not original-pixel equivalence.

The final screenshot reuses structured source/draw readiness. A strict,
read-only held snapshot preserves the owner's active progress subscription;
final exported accounting still requires its separate freeze after owner
closure. Exact driver, native, status and accounting operands are retained for
rejection analysis. All native owners, input acquisition, transports, browsers,
HTTP server and the Room Worker closed. Root freshly checked 25 attributable
PIDs and five groups absent, ports reusable, and retained then released both
exact lane markers. The lane is free after that review.

The focused boundary checks passed 168 Node tests and one Python contract test.
The unfiltered suite on the capture producer passed 1,948 executed tests with
153 skipped (2,101 discovered), zero failures, in 485.873 seconds. The fresh
ordinary Release build passed with all ten diagnostic/profile flags off; its
102-file frozen build/site/source/recipe closure is separate from the preserved
historical closure. The documentation follow-up needs only link, path, diff
and publication-content checks; no new capture or build follows it.

Attempts 01 and 02 remain failed. Attempt 01 exposed the ordinary Gamepad
fixture-kind decoder rejection, repaired with the actual controller-init-script
regression. Attempt 02 failed before the final route-boundary screenshot and
before checksum export: zero exported records, with exact failed readiness
operands absent. Its finally-path PNGs are not qualifying boundary proof. Its
saved Worker `passed=false` incorporates the failed overall run; the
independently checked worker cleanup operands succeeded. Neither failure is
relabeled. A root administrative assertion initially treated the attempt03
endpoint-error role mapping as a list; it was corrected before mutations and
is recorded separately from the successful capture, with no retry.

This closes the declared active-match prerequisite only. Internet and
remote-machine play, physical input, foreground timing, performance,
uninterrupted audio/PCM, original pixels or gameplay comparison, Results,
return to CSS and whole-session or competitive-set acceptance remain open.
The [roadmap](../ROADMAP.md#current-priorities) and
[accuracy contract](../ACCURACY_CONTRACT.md) keep these gates separate.
