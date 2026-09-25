# Corresponding-source licensing

The observer is a downstream diagnostic addition to Dolphin Emulator commit
`c77bbaa0f372c3f72281602a8b087206706542cb`. Dolphin Emulator is distributed
under the GNU General Public License, version 2 or later (GPL-2.0-or-later).
The patch and overlay retain the upstream copyright notices and SPDX headers.

The allocation-history extension is part of the same downstream reference
boundary. Its tracked patch and overlay files are:

- `patches/0003-allocation-observer.patch`
- `patches/0004-allocation-followed-returns.patch`
- `source/Core/PowerPC/ReferenceAllocationObserver.cpp`
- `source/Core/PowerPC/ReferenceAllocationObserver.h`
- `source/Core/PowerPC/ReferenceAllocationProfile.h`

The observer sources retain GPL-2.0-or-later SPDX headers. The profile is a
generated identity-only header: it records the pinned DOL/source identity,
function and global metadata, return boundaries and hashes, and does not embed
original executable bodies. It remains source-derived metadata associated with
the reference build and does not supply a separate license for the recovered
DOL, Melee/SDK source or its inputs. Keep these paths, their patch/overlay
hashes and the generated build manifest with any reference binary.

The build helper records the pinned commit, patch hashes, overlay hashes,
composed source diff, compiler/CMake details, and resulting binary hash in its
JSON manifest. Keep that manifest with any binary supplied to the capture app
so the corresponding source can be reconstructed with
`scripts/build_reference_dolphin.py`.
