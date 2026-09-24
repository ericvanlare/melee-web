# Repository content check

The [repository guard](../scripts/check_repository_content.py) checks every
entry in the Git index, including force-added files. It reads Git blobs, so
unstaged working-tree edits, ignored local inputs and symlink targets cannot
change the snapshot being checked. It needs only Python 3 and Git.

Stage the intended changes, then run:

```sh
python3 scripts/check_repository_content.py
```

To check a committed tree, use:

```sh
python3 scripts/check_repository_content.py --ref HEAD
```

The policy is read from the same index or commit as the content. A commit that
predates the policy fails as incomplete; this command is not an all-history
audit. Exit codes are `0` for a completed check with no findings, `1` for
findings, and `2` when the snapshot or policy could not be checked completely.
Diagnostics give an escaped path, rule and applicable line number, without
printing matched contents. Recognized secrets in filenames cause path redaction.

## Rejection boundaries

The check rejects:

- Disc, extracted-game, compiled and archive filename extensions listed in
  the script, ignoring extension case; private input directories, credential
  filenames and local environment files other than `.env.example`.
  Reserved paths include local reference-capture state such as `captures/`,
  `private/`, `secrets/`, `states/`, `memorycards/`, `config/`, `configuration/`
  and the `config.json`, `dolphin.ini` and `gcpadnew.ini` filenames.
- NUL-containing or non-UTF-8 blobs, including binary payloads renamed to text
  extensions; `.b64` files and long contiguous base64 payloads in other files.
- Recognized private-key headers, selected AWS/GitHub/Slack/API token formats,
  long quoted credential assignments and literal personal home-directory paths.
- Symlinks, submodules, unsafe path characters and individual blobs over 8 MiB.
  The size limit is an explicit operational guard; large blobs are rejected
  before their contents are read. Unmerged indexes fail as incomplete.

These are bounded checks, not a claim that every secret, game asset or encoding
can be recognized. Short fragments, wrapped or alternative encodings, unfamiliar
credential formats and semantically copied source/data still need review. The
guard does not assess ownership, licenses, commit metadata, history, issues,
attachments, Actions logs/artifacts or hosted packages. Those remain in the
[publication checklist](PUBLIC_REPOSITORY_CHECKLIST.md).

## Review an exception

The tracked [policy](../.github/repository-content-policy.json) records exact
paths, rules, SHA-256 identities and reasons. Review the actual content and its
provenance before proposing an exception. Never add one just to turn CI green.

File exceptions can waive only `binary_content`, `encoded_payload` or
`asset_extension` for the exact full-file hash at the exact path. Changed bytes
or a renamed copy need a new review. Other checks, including the secret scan,
still apply to an excepted file. The initial entries cover three reviewed
project UI screenshots and the existing renderer pipeline seed. The seed's
content exception does not resolve the generated-data licensing decision in
[the source inventory](SOURCE_LICENSE_INVENTORY.md).

Synthetic secret fixtures use the `synthetic_secrets` list: the identity covers
the exact matched token, path and rule, rather than exempting the whole file.
For a quoted credential assignment, hash the value without its quotes. A
private-key header cannot be excepted this way. Prefer assembling nonfunctional
test samples at runtime, as the guard's own tests do, instead of embedding
credential-shaped literals in source. Never hash or add a real secret as an
exception; remove it from the proposed snapshot and address its exposure.

Policy changes receive the same provenance and content review as the material
they permit. The guard cannot prevent a contributor from proposing changes to
its own policy or implementation; required review and branch protection remain
separate GitHub controls.

## Verification

The focused tests create real temporary Git repositories and exercise staged
versus unstaged bytes, commit-specific policy, forced additions, binary/encoded
content, exception scoping, symlinks, submodules, unmerged entries, large blobs
and diagnostics that do not disclose detected values:

```sh
python3 -m unittest discover -s tests -p test_repository_content.py -v
```

The `repository-content` job in [Verify](../.github/workflows/verify.yml) runs
the snapshot check and its focused tests before the existing build/test jobs.
The aggregate `browser-build` job includes its result and requires all of its
dependencies to succeed. Requiring that aggregate in GitHub remains a separate
cutover step; a local test pass does not establish a configured branch rule.

For deleted files, historical path aliases, branches and PR refs, use the
separate [history audit procedure](REPOSITORY_HISTORY_AUDIT.md). It binds the
policy and selected refs to a report and leaves historical findings visible
for explicit review instead of broadening current-tree exceptions.
