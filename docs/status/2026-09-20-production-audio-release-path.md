# Production audio release path

PR #42 is merged on top of #49. The owner approved promoting replacement
audio to production independently of Results PR #44. The production package
now has an explicit `audio-player` identity and uses the same restricted Release
audio runtime as the combined listening preview. Its auditor binds the native
source/tool/seed identity, exact audio module inventory, production notices and
hosting policy. The legacy silent profile remains available for rollback.
See [production audio release](../AUDIO_PRODUCTION.md) for the commands and
verification gates. The combined preview's existing evidence remains scoped to
[its recorded browser and PCM checks](../evidence/audio-main-integration-v1.json);
it does not establish full-match performance or original hardware fidelity.
