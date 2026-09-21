# Security reporting

The supported security-review target is the current default branch. This is an
experimental project; no response-time or long-term support commitment is made.
Reports about native/Wasm parsers, local-file handling, browser isolation,
dependency loading or unintended network access are particularly useful.

Use GitHub's [private vulnerability reporting form](https://github.com/ericvanlare/melee-web/security/advisories/new)
once **Report a vulnerability** is available. Send reproduction steps, the
affected commit/build, environment and impact. Prefer a minimal synthetic input;
do not attach disc images, extracted copyrighted game assets or credentials.
Coordinate any necessary sensitive test input through the private advisory.

The repository is currently private and the private-reporting feature could
not be verified. Enabling it and verifying the reporting form are required
steps in the [public cutover](docs/PUBLICATION_CUTOVER.md). Until that route is
available, do not post vulnerability details in an issue, pull request or other
public channel. There is no verified alternative private inbox published here.

Ordinary gameplay bugs without a security impact can use normal issues after
publication. Security reports should remain private until a fix or coordinated
disclosure plan is agreed.
