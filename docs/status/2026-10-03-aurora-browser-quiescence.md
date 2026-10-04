# Aurora browser ownership descriptor

**Compiled / Source identified / Browser exercised**

The [current receipt](../evidence/aurora-browser-quiescence-v2.json) covers code
head `c5a465a` on main base `c3ddd39`, on the Apple M4 Mac mini with Chrome
154.0.8037.97 in headless mode. The diagnostic owner book, mutex, descriptor
producer and boundary lease are OFF in ordinary builds. The opt-in Release
profile uses a separate `build/browser-quiescence-release` directory.

The schema records all authored frame/staging slots, packet and shadow
ByteBuffer metadata, staging generation, submission/callback sequences and
returned WebGPU Future, queue and encoder identities. Row counts, byte sizes,
offsets and strides come from the native descriptor. These opaque identities
are observations; they do not reconstruct host objects.

The actual source Mario/Final Destination fixture followed original Entry/Ready
and drew source frames 1 and 2. Two idle captures matched byte for byte.
Invalid inputs, a deliberate descriptor perturbation, and descriptors/leases
from the previous source boundary were refused. Host polling kept source
counters unchanged; one ordinary source step moved frame/ticks/generation from
`1/125/1` to `2/126/1`. The descriptor was 1160 bytes and the lease 80 bytes.

Ordinary and diagnostic Release builds passed. The retained rendered build
transaction took 55.051 seconds, reversed its temporary CMake change exactly,
and preserved input hashes. The browser transaction passed in 2.952 seconds;
the independent audit verified 31 file pins, five trees, eight Chrome process
absences, the Node process group and the closed HTTP listener. The screenshot
was inspected: both fighters and the stage render, with known magenta geometry.
No original pixel comparison was performed. The receipt retains a resource
404 without a request URL and the Aurora shutdown diagnostics separately from
page errors; the failed resource cannot be identified from this capture.

The required suite passed: 1883 tests, 141 skipped, 348.523 seconds in unittest
(348.859 seconds through the owned wrapper). All 1699 tracked source pins
remained unchanged. Eight private transaction/cleanup controls also passed.
The receipt binds artifact and report hashes. Generated JavaScript was unmodified.

After normal bootstrap, reproduce the public focused checks and build:

```sh
python3 scripts/agent_workspace.py run -- python3 -m unittest discover -s tests -p 'test_aurora_browser_*' -v
python3 scripts/build.py --target aurora-browser-quiescence --configuration Release
```

The rendered receipt uses a retained private source fixture and locally owned
assets; this PR adds no public browser reproduction target. The
[historical receipt](../evidence/aurora-browser-quiescence-v1.json) remains bound
to old code head `08366be` and base `e7c15a`; it is not validation of this revision.

Descriptor/lease polling can process queued completions and submits no new draw.
It refuses pthread builds because packet/shadow reads lack a synchronized
threaded boundary. The owner book rejects mismatched and duplicate completions;
old-generation callbacks cannot mutate reused slots.

No restore consumes these records. Wasm-resident counters cannot provide
independent host authority after heap restoration. WebGPU/Promise/finalizer,
queue/fence/submission, presentation, pipeline, audio, allocator/buffer-content
and complete native host ownership still need reversible contracts.
`full_renderer_restore_safe` remains false. Complete gameplay/rematch, impaired
rollback, original pixels/PCM, physical input, Internet and foreground
performance remain unrun here; [multiplayer acceptance](https://github.com/ericvanlare/melee-web/issues/115)
is open.
