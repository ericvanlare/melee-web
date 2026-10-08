# B4 Koopa up-B callback ABI fixture

**Compiled / Source identified**

Issue [#265](https://github.com/ericvanlare/melee-web/issues/265) follows a
retained v10 browser runtime failure before the requested stock-decrement
boundary. The frozen browser failed at the first indirect call in
`ftCommon_8007DB58`, which invokes `take_dmg_cb`. Koopa's up-B registration
casts the empty `ftKp_Init_80132B38(void)` provider into that one-argument
callback slot. The retained final frame has Koopa in aerial up-B, but the
runtime callback pointer was not exported: that provider's participation in
the browser failure remains an inference from the source and trace.

The downstream patch supplies a typed `void(HSD_GObj*)` adapter for both
`take_dmg_cb` and `death2_cb`. It discards the unused argument and calls the
original empty provider, preserving its body and consumer order. Pristine
upstream source is unchanged.

The asset-free Wasm/Node fixture extracts the prepared registration, damage
consumer and provider with the original Fighter/GObj types. Its historical
cast control traps with a function-signature mismatch. The adapter control
passes callback registration and order checks. Only this fixture instruments
the otherwise empty provider to observe its invocation. The two pre-callback
services are omitted and replaced with order-recording fixture functions;
no game assets or gameplay services are reached. The scoped receipt is
[Koopa callback ABI fixture](../evidence/b4-koopa-callback-abi-v1.json).

The first fixture compile omitted the original `GET_FIGHTER` inline header;
that failure was retained before correcting the include. Both fixture
variants then compiled, the expected negative trap was retained, and the
positive fixture passed. Patch canonicalization checks also passed.

The historical comparator branch and failed browser captures are preserved.
The retained Release runtime still predates this adapter. The full suite,
updated Release build, browser retry and original MWRO comparison are unrun;
this component fixture does not establish stock-decrement or session accuracy.
