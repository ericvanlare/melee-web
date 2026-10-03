# Save import size preflight

**Browser exercised** for the Settings import boundary: the UI rejects a
90,177-byte file before `File.arrayBuffer()`, shows no confirmation, and leaves
the active mode, Personal profile and source-session actions unchanged. A
90,176-byte synthetic GCI still follows the normal validation, confirmation,
atomic import and source restart path. The [bounded receipt](../evidence/save-profile-oversized-import-v1.json)
records the observed calls and hashes. This is input-boundary evidence; the
synthetic GCI is not Melee/Dolphin interoperability evidence.
