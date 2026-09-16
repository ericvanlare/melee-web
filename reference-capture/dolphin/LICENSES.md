# Corresponding-source licensing

The observer is a downstream diagnostic addition to Dolphin Emulator commit
`c77bbaa0f372c3f72281602a8b087206706542cb`. Dolphin Emulator is distributed
under the GNU General Public License, version 2 or later (GPL-2.0-or-later).
The patch and overlay retain the upstream copyright notices and SPDX headers.

The build helper records the pinned commit, patch hashes, overlay hashes,
composed source diff, compiler/CMake details, and resulting binary hash in its
JSON manifest. Keep that manifest with any binary supplied to the capture app
so the corresponding source can be reconstructed with
`scripts/build_reference_dolphin.py`.
