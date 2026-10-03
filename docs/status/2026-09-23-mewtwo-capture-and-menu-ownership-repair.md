# Mewtwo capture and menu ownership repair

The Mewtwo Confusion victim command rows and menu rumble/GObj lifetime repairs
are validated at their declared boundaries. **Native traced**: the original
menu/four-stock lifecycle completes twice with cancellation and teardown.
**Browser exercised**: Release and RelWithDebInfo both select Mewtwo through
the original CSS, cancel SSS, capture/release Mario with Confusion and unload.
See the [merge review receipt](../evidence/mewtwo-crash-merge-review-v1.json)
for exact builds, retained failures and integration checks. This repairs a
crash; independent equivalence, full fighter admission and deployment are
separate gates.
