"""Compare independently declared Setup fields with one already selected slice.

Producer pins, stream validation and phase selection belong to the caller.
This helper reuses the retail decoder without relaxing its ordinary-VS,
size, callback-pointer or active-port guards. Agreement covers declared fields
only; it establishes no opaque-byte, ordering, RNG, GO or gameplay equivalence.
"""
from __future__ import annotations
import hashlib
from typing import Any
import retail_setup_validation as SETUP


class StadiumSetupAuditError(ValueError):
    def __init__(self, code: str, message: str, **details: Any):
        super().__init__(message)
        self.code = code
        self.details = details


def compare_setup_fields(expected_setup: Any, start_melee_hex: Any) -> dict[str, Any]:
    """Validate both objects, then stop at the first declared-field mismatch."""
    try:
        expected = SETUP._validate_expected_setup(expected_setup)
    except SETUP.SetupValidationError as exc:
        raise StadiumSetupAuditError("invalid_expected_setup", str(exc)) from exc
    try:
        actual = SETUP._decode_setup(start_melee_hex)
    except SETUP.SetupValidationError as exc:
        raise StadiumSetupAuditError("existing_setup_decoder_guard_failed", str(exc)) from exc
    mismatch = SETUP._first_mismatch(expected, actual)
    if mismatch is not None:
        path, wanted, observed = mismatch
        raise StadiumSetupAuditError(
            "declared_setup_field_mismatch", f"first declared Setup mismatch at {path}",
            path=path, expected=wanted, actual=observed,
        )
    return {
        "scope": "declared_setup_fields_only",
        "expected_setup_from_source_spec": expected,
        "decoded_setup_fields": actual,
        "slice_sha256": hashlib.sha256(bytes.fromhex(start_melee_hex)).hexdigest(),
        "declared_fields_match": True,
        "first_mismatch": None,
    }
