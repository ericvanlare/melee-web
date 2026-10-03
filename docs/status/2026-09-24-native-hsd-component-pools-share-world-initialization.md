# Native HSD component pools share world initialization

**Compiled / Source identified / Native traced** for the bounded component-pool
lifecycle: native scene owners share the original component initialization order
instead of reinitializing Shadow and ZList at individual entries. See the
[scoped receipt](../evidence/native-pool-initialization-v1.json) for lifecycle
checks, the diagnostic browser prefix and retained failures. This does not
provide live original heap identities or CPU register carry; full-session
browser equivalence remains open. No deployment was performed.
