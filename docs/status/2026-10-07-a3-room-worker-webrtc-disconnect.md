# A3 native disconnect over Room Worker-signaled WebRTC

**Source identified / Browser exercised**

The [portable receipt](../evidence/a3-room-worker-webrtc-disconnect-v1.json)
records the bounded CSS disconnect experiment for
[issue #238](https://github.com/ericvanlare/melee-web/issues/238). The repaired
capture passed on clean source `33b8d80`, based on main `d8f7600`. Both pages
owned their lockstep peer and reliable ordered, host-only WebRTC DataChannel.
Offer and answer stayed in the pages and traveled through the existing local
RoomRelay Worker. Node coordinated lifecycle and the recorded input recipe.

The retained MWNI v1 recipe contains 5,082 inputs. This experiment selected five
input slots, published inputs 0–3 and withheld input 4. Four published inputs plus
the two neutral bootstrap ticks produced six consumed source ticks within a
seven-tick source capacity. Both peers actually acknowledged input 3 before beta
closed its channel at native cursor/pushed 6 and `network_wait`. The injection
event was retained by the actual report writer. Both native terminals then held
kind 2, tick 0 and channel 0, with cursor and pushed count unchanged at 6 through
a 123 ms observation window.

Each peer exported six contiguous 64-byte native checksum records, indices 0–5,
and the raw streams matched. Each compared four remote checksums: local count 6,
remote count 4, next comparison 4 and remote checksum ACK 3. All six exported
records were submitted while the peer was active; zero were classified as
post-terminal native evidence. The two remaining raw records are retained
separately from protocol receipts. Closing the channel did not establish later
checksum comparisons or acknowledgements. Existing source accounting validated
exactly six source steps and draws per peer with contiguous callback records.

The native runtime bytes were reused from ordinary Release producer `0d8556e`;
this was not a new native build at `33b8d80`. Exact runtime target inputs were
unchanged. All 37 staged and source file identities matched prior staging:
32 native runtime files and five peer modules, with all ten diagnostic opt-ins
OFF. The final Python suite passed 2,029 tests with 151 skipped. Focused tests
exercise the actual injection action and finalizer, including close failure and
missing pre-close ACK. A reproducer using the failed source and actual retained
report verifies the old export omission. Strict coordinator tests reject 23
metadata and six diagnostics mutations; six Worker path cases also passed.
Those pure fixtures are preparation evidence, not additional browser runs.

Each peer retained a live held original CSS screenshot, phase 1 at source
cursor 6, before unload. Both 900×700 images have 640×480 canvases and positive
WebGPU/isolation observations. Reviewed images retain red geometry along the
right roster edge and lower viewport clipping. Each repaired screenshot's audio
overlay displays 22 underrun frames. This is one snapshot per peer, not a
complete or final underrun witness. Audio processing remained enabled with
speakers muted; no PCM, uninterrupted-audio or audible-output comparison was
performed. Source accounting, the held terminal and the absence of timing
resumes do not establish foreground timing, latency or performance.

The original capture on `a907d41`, based on main `5041d6b`, remains a failed
coordinator result. Its runner observed the expected native disconnect, six raw
records, four comparisons and ACK 3, but the action wrote its injection event on
an instance-row property that the finalizer never exported. The strict
coordinator rejected the missing top-level field. The earlier synthetic fixture
had supplied that field directly and missed the reporter defect. The repair
records the actual event on the existing exported result at the action; the
validator requirement and terminal/transport semantics remain intact. Root
separately authorized the repaired capture after source, tests, staging and
packet review. The failed artifacts and both failed profiles remain retained;
their screenshots show 107 underrun frames each, independently of the repaired
screenshots. The failed launch also lacks exact outer wrapper/coordinator PID
identities; that historical gap remains explicit.

The repaired capture recorded those outer identities at launch, joined the
wrapper and verified its process group absent. Root independently verified all
28 recorded process IDs and seven groups absent, three ports bindable,
Worker-owned paths absent and Chrome temporary storage empty. The repaired
profiles were removed; all 37 served identities matched before and after over
HTTP and local storage. Endpoint, callback, transport, page and cleanup errors
were empty. Root released the exact timing reservation after independent review.
The interrupted first suite and first reporter-test fixture failure remain
retained. The [issue #231 receipt](../evidence/a3-room-worker-webrtc-input-sampling-v1.json)
also retains its unrecovered frozen-source-manifest overwrite gap; fresh freezes
do not reconstruct historical evidence.

This is same-host recorded CSS disconnect component evidence. It does not
exercise desync or a full-route negative control. Internet/two-machine play,
STUN/TURN, relay fallback, physical/live input, foreground timing, performance,
rendering fidelity, continuous audio, retail comparison and general
whole-session accuracy remain open. The broader A3 gate in the
[roadmap](../ROADMAP.md) is not closed by this result.
