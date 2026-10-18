# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent rational oracle and frozen vectors for UCF DBOOC SquatRv.

This module performs no C calls, host floating-point arithmetic, or gameplay
source execution.  It freezes the finite binary32 arithmetic contract only;
it is not evidence of PPC exception behavior or live UCF activation.
"""

from fractions import Fraction
import unittest


EPSILON_BITS = 0x38D1B717
SCALE_BITS = 0x42A00000
OVERRIDE_THRESHOLD_BITS = 0x3F170A3D
COMMON_THRESHOLD_BITS = 0x3F200000  # Explicit synthetic 0.625.
UINT32_MASK = 0xFFFFFFFF
SIGN_BIT = 0x80000000


def _binary32_fraction(bits):
    """Decode finite IEEE-754 binary32 bits to an exact Fraction."""
    if not isinstance(bits, int) or bits < 0 or bits > UINT32_MASK:
        raise ValueError(f"not a binary32 word: {bits!r}")
    negative = bool(bits & SIGN_BIT)
    exponent = (bits >> 23) & 0xFF
    significand = bits & 0x7FFFFF
    if exponent == 0xFF:
        raise ValueError("oracle domain excludes NaN and infinity")
    if exponent == 0:
        value = Fraction(significand, 1 << 149)
    else:
        significand |= 1 << 23
        shift = exponent - 150
        value = (
            Fraction(significand << shift, 1)
            if shift >= 0
            else Fraction(significand, 1 << -shift)
        )
    return -value if negative else value


def _round_nonnegative_integer_ties_even(value):
    quotient, remainder = divmod(value.numerator, value.denominator)
    doubled_remainder = remainder << 1
    if doubled_remainder > value.denominator:
        return quotient + 1
    if doubled_remainder == value.denominator and quotient & 1:
        return quotient + 1
    return quotient


def _floor_log2(value):
    """Return floor(log2(value)) for a positive rational, using integers."""
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    if exponent >= 0:
        if value < (1 << exponent):
            exponent -= 1
        while value >= (1 << (exponent + 1)):
            exponent += 1
    else:
        if value < Fraction(1, 1 << -exponent):
            exponent -= 1
        while value >= Fraction(1, 1 << -(exponent + 1)):
            exponent += 1
    return exponent


def _scale_by_power_of_two(value, power):
    if power >= 0:
        return value * (1 << power)
    return value / (1 << -power)


def _round_binary32_ties_even(value):
    """Round an exact rational to IEEE-754 binary32, RN-even, with integer math."""
    negative = value < 0
    magnitude = -value if negative else value
    sign = SIGN_BIT if negative else 0
    if magnitude == 0:
        return sign

    exponent = _floor_log2(magnitude)
    if exponent >= -126:
        significand = _round_nonnegative_integer_ties_even(
            _scale_by_power_of_two(magnitude, 23 - exponent),
        )
        if significand == (1 << 24):
            significand >>= 1
            exponent += 1
        if exponent > 127:
            return sign | 0x7F800000
        if exponent < -126:
            raise AssertionError("normal rounding unexpectedly crossed subnormal range")
        return sign | ((exponent + 127) << 23) | (significand - (1 << 23))

    subnormal_significand = _round_nonnegative_integer_ties_even(
        magnitude * (1 << 149),
    )
    if subnormal_significand >= (1 << 23):
        return sign | 0x00800000
    return sign | subnormal_significand


def _require_axis(bits):
    value = _binary32_fraction(bits)
    if abs(value) > 1:
        raise ValueError("oracle axis domain is finite binary32 in [-1, 1]")
    return value


def _epsilon():
    return _binary32_fraction(EPSILON_BITS)


def _fmsubs_axis_bits(axis_bits):
    """Exact |axis|*80-epsilon followed by one binary32 RN-even rounding."""
    axis = abs(_require_axis(axis_bits))
    scale = _binary32_fraction(SCALE_BITS)
    return _round_binary32_ties_even(axis * scale - _epsilon())


def _split_multiply_subtract_mutation_bits(axis_bits):
    """Test-only wrong implementation: round multiply before subtraction."""
    axis = abs(_require_axis(axis_bits))
    product_bits = _round_binary32_ties_even(
        axis * _binary32_fraction(SCALE_BITS),
    )
    product = _binary32_fraction(product_bits)
    return _round_binary32_ties_even(product - _epsilon())


def _truncate_toward_zero(value):
    if value < 0:
        return -((-value.numerator) // value.denominator)
    return value.numerator // value.denominator


def _quantized_axis(axis_bits):
    rounded_bits = _fmsubs_axis_bits(axis_bits)
    return (_truncate_toward_zero(_binary32_fraction(rounded_bits)) + 2) & UINT32_MASK


def _radius_sum_u32(x_bits, y_bits):
    qx = _quantized_axis(x_bits)
    qy = _quantized_axis(y_bits)
    return ((qx * qx) + (qy * qy)) & UINT32_MASK


def _signed32(value):
    value &= UINT32_MASK
    return value if value < 0x80000000 else value - 0x100000000


def _select_threshold_bits(timer, x_bits, y_bits, common_bits):
    """Apply the unsigned-byte zero gate and signed strict radius comparison."""
    if not isinstance(timer, int) or not 0 <= timer <= 0xFF:
        raise ValueError("timer must be an unsigned byte")
    _binary32_fraction(common_bits)
    if timer == 0 and _signed32(_radius_sum_u32(x_bits, y_bits)) > 6400:
        return OVERRIDE_THRESHOLD_BITS
    return common_bits


def _squat_rv_continue(stick_y_bits, positive_threshold_bits):
    """Original strict predicate: stick_y > -threshold."""
    stick_y = _require_axis(stick_y_bits)
    threshold = _binary32_fraction(positive_threshold_bits)
    return stick_y > -threshold


def _integer_transition_boundary(integer):
    """Exact axis boundary where rounded FMSUBS first reaches an integer.

    Use the midpoint from the integer's binary32 predecessor.  At powers of
    two, this spacing is half the spacing *above* the integer.  Every integer
    here has an even trailing significand bit, so the exact midpoint rounds
    to the integer, making the input boundary inclusive.
    """
    if not 1 <= integer <= 80:
        raise ValueError("transition integer must be in [1, 80]")
    integer_bits = _round_binary32_ties_even(Fraction(integer))
    if integer_bits & 1 or _binary32_fraction(integer_bits) != integer:
        raise AssertionError("expected an exactly represented even integer")
    predecessor = _binary32_fraction(integer_bits - 1)
    output_midpoint = (predecessor + integer) / 2
    return (output_midpoint + _epsilon()) / _binary32_fraction(SCALE_BITS)


def _first_axis_bits_at_least(value):
    """Find a positive finite-domain input by exact ordered-word bisection.

    This search does not use the oracle's rounding routine to choose input
    neighbors; that routine remains the operation being checked.
    """
    if not 0 <= value <= 1:
        raise ValueError("axis search value must be in [0, 1]")
    lower, upper = 0, 0x3F800000
    while lower < upper:
        middle = (lower + upper) // 2
        if _binary32_fraction(middle) < value:
            lower = middle + 1
        else:
            upper = middle
    return lower


class DboocBinary32OracleTests(unittest.TestCase):
    def test_constants_ties_and_binary32_domain_edges(self):
        self.assertEqual(_binary32_fraction(SCALE_BITS), Fraction(80, 1))
        self.assertEqual(_binary32_fraction(EPSILON_BITS), Fraction(13743895, 1 << 37))
        self.assertEqual(_binary32_fraction(OVERRIDE_THRESHOLD_BITS), Fraction(9898557, 1 << 24))

        # Exact midpoints exercise even-significand tie selection in both directions.
        self.assertEqual(
            _round_binary32_ties_even(Fraction(1) + Fraction(1, 1 << 24)),
            0x3F800000,
        )
        self.assertEqual(
            _round_binary32_ties_even(Fraction(1) + Fraction(3, 1 << 24)),
            0x3F800002,
        )
        # Subnormal ties, the carry into the minimum normal, and the
        # asymmetric predecessor spacing at the carry into 1.0.
        for value, expected in (
            (Fraction(1, 1 << 150), 0),
            (Fraction(3, 1 << 150), 2),
            (Fraction((1 << 24) - 1, 1 << 150), 0x00800000),
            (Fraction(1) - Fraction(1, 1 << 25), 0x3F800000),
        ):
            self.assertEqual(_round_binary32_ties_even(value), expected)
            self.assertEqual(_round_binary32_ties_even(-value), expected | SIGN_BIT)

        # Signed zeros, subnormal limits, and both signs of the domain endpoint.
        for axis_bits in (
            0x00000000, 0x80000000, 0x00000001, 0x80000001,
            0x007FFFFF, 0x807FFFFF,
        ):
            self.assertEqual(_fmsubs_axis_bits(axis_bits), 0xB8D1B717, hex(axis_bits))
        self.assertEqual(_fmsubs_axis_bits(0x3F800000), 0x429FFFF3)
        self.assertEqual(_fmsubs_axis_bits(0xBF800000), 0x429FFFF3)
        self.assertEqual(_quantized_axis(0), 2)
        self.assertEqual(_quantized_axis(0x3F800000), 81)
        self.assertEqual(_quantized_axis(0xBF800000), 81)

        # Epsilon / 80 is itself exactly representable.  Cancellation is
        # +zero after fabs, for either sign of the input; adjacent inputs
        # stay in the truncation-to-zero cell.
        cancellation = _epsilon() / _binary32_fraction(SCALE_BITS)
        cancellation_bits = _first_axis_bits_at_least(cancellation)
        self.assertEqual(_binary32_fraction(cancellation_bits), cancellation)
        for sign in (0, SIGN_BIT):
            self.assertEqual(_fmsubs_axis_bits(cancellation_bits | sign), 0)
            for neighbor in (cancellation_bits - 1, cancellation_bits, cancellation_bits + 1):
                self.assertEqual(_quantized_axis(neighbor | sign), 2)

        for invalid in (0x7F800000, 0xFF800000, 0x7FC00000, 0x3F800001, 0xBF800001):
            with self.assertRaises(ValueError):
                _fmsubs_axis_bits(invalid)

    def test_fused_integer_transition_breakpoint_neighbors(self):
        # The exact input transition for truncation to n is
        # (midpoint(predecessor(n), n) + epsilon) / 80.  These breakpoints are
        # not themselves binary32 inputs; the frozen neighboring inputs show
        # the RN-even FMA followed by truncation and +2.
        vectors = (
            {
                "n": 6,
                "breakpoint": Fraction(824647431959, 10995116277760),
                "lower_bits": 0x3D999A40,
                "upper_bits": 0x3D999A41,
                "lower_fmsubs": 0x40BFFFFE,
                "upper_fmsubs": 0x40C00000,
                "lower_q": 7,
                "upper_q": 8,
            },
            {
                "n": 46,
                "breakpoint": Fraction(6322205341463, 10995116277760),
                "lower_bits": 0x3F133347,
                "upper_bits": 0x3F133348,
                "lower_fmsubs": 0x4237FFFF,
                "upper_fmsubs": 0x42380000,
                "lower_q": 47,
                "upper_q": 48,
            },
        )
        for vector in vectors:
            self.assertLess(_binary32_fraction(vector["lower_bits"]), vector["breakpoint"])
            self.assertGreater(_binary32_fraction(vector["upper_bits"]), vector["breakpoint"])
            self.assertEqual(_fmsubs_axis_bits(vector["lower_bits"]), vector["lower_fmsubs"])
            self.assertEqual(_fmsubs_axis_bits(vector["upper_bits"]), vector["upper_fmsubs"])
            self.assertEqual(_quantized_axis(vector["lower_bits"]), vector["lower_q"])
            self.assertEqual(_quantized_axis(vector["upper_bits"]), vector["upper_q"])

    def test_every_reachable_integer_transition_and_domain_end(self):
        # For |axis| <= 1, the only reachable positive-integer truncation
        # transitions are 1..79.  The tiny negative cell truncates to zero;
        # 80's transition is beyond the allowed endpoint.
        for integer in range(1, 80):
            with self.subTest(integer=integer):
                boundary = _integer_transition_boundary(integer)
                self.assertGreater(boundary, 0)
                self.assertLess(boundary, 1)
                upper_bits = _first_axis_bits_at_least(boundary)
                lower_bits = upper_bits - 1
                self.assertLess(_binary32_fraction(lower_bits), boundary)
                self.assertGreaterEqual(_binary32_fraction(upper_bits), boundary)
                for sign in (0, SIGN_BIT):
                    self.assertEqual(_quantized_axis(lower_bits | sign), integer + 1)
                    self.assertEqual(_quantized_axis(upper_bits | sign), integer + 2)
        self.assertGreater(_integer_transition_boundary(80), 1)
        with self.assertRaises(ValueError):
            _first_axis_bits_at_least(_integer_transition_boundary(80))

    def test_split_mutation_has_bit_only_and_integer_decision_witnesses(self):
        # This input kills separate multiply/subtract in the integer decision:
        # FMSUBS rounds to 6.0 (q=8), while the split path rounds just below 6
        # (q=7).
        axis_bits = 0x3D999A41
        self.assertEqual(_fmsubs_axis_bits(axis_bits), 0x40C00000)
        self.assertEqual(_quantized_axis(axis_bits), 8)
        self.assertEqual(_split_multiply_subtract_mutation_bits(axis_bits), 0x40BFFFFF)
        self.assertEqual(
            (_truncate_toward_zero(
                _binary32_fraction(_split_multiply_subtract_mutation_bits(axis_bits)),
            ) + 2) & UINT32_MASK,
            7,
        )

        # At this second input the fused and split results differ by one bit,
        # but truncation and +2 agree.  This distinguishes a bit mismatch from
        # an observable integer-decision mismatch.
        bit_only_axis = 0x3F133346
        fused = _fmsubs_axis_bits(bit_only_axis)
        split = _split_multiply_subtract_mutation_bits(bit_only_axis)
        self.assertEqual((fused, split), (0x4237FFFD, 0x4237FFFE))
        self.assertNotEqual(fused, split)
        self.assertEqual(_quantized_axis(bit_only_axis), 47)
        self.assertEqual(
            (_truncate_toward_zero(_binary32_fraction(split)) + 2) & UINT32_MASK,
            47,
        )

    def test_strict_radius_boundary_and_unsigned_timer_gate(self):
        x_bits = 0x3F147AE1
        radius_vectors = (
            (0x3F451EB8, 48, 63, 6273, False),
            (0x3F47AE14, 48, 64, 6400, False),
            (0x3F4A3D71, 48, 65, 6529, True),
        )
        for y_bits, qx, qy, expected_sum, override in radius_vectors:
            self.assertEqual(_quantized_axis(x_bits), qx)
            self.assertEqual(_quantized_axis(y_bits), qy)
            self.assertEqual(_radius_sum_u32(x_bits, y_bits), expected_sum)
            expected = OVERRIDE_THRESHOLD_BITS if override else COMMON_THRESHOLD_BITS
            self.assertEqual(_select_threshold_bits(0, x_bits, y_bits, COMMON_THRESHOLD_BITS), expected)
            self.assertEqual(_select_threshold_bits(0, x_bits | SIGN_BIT, y_bits | SIGN_BIT,
                                                    COMMON_THRESHOLD_BITS), expected)

        above_radius_y = radius_vectors[-1][0]
        for timer in (1, 2, 127, 128, 255):
            self.assertEqual(
                _select_threshold_bits(timer, x_bits, above_radius_y, COMMON_THRESHOLD_BITS),
                COMMON_THRESHOLD_BITS,
                f"unsigned timer {timer} must bypass the zero-only override",
            )
        for invalid_timer in (-1, 256):
            with self.assertRaises(ValueError):
                _select_threshold_bits(invalid_timer, x_bits, above_radius_y, COMMON_THRESHOLD_BITS)

    def test_synthetic_threshold_causal_branch_and_adjacent_stick_y(self):
        x_bits = 0x3F800000
        selected_y = 0xBF170A3D  # Exactly -override threshold.
        self.assertGreater(_signed32(_radius_sum_u32(x_bits, selected_y)), 6400)
        self.assertTrue(_squat_rv_continue(selected_y, COMMON_THRESHOLD_BITS))
        self.assertFalse(_squat_rv_continue(selected_y, OVERRIDE_THRESHOLD_BITS))
        self.assertEqual(
            _select_threshold_bits(0, x_bits, selected_y, COMMON_THRESHOLD_BITS),
            OVERRIDE_THRESHOLD_BITS,
        )
        self.assertEqual(
            _select_threshold_bits(1, x_bits, selected_y, COMMON_THRESHOLD_BITS),
            COMMON_THRESHOLD_BITS,
        )

        # Around -0.625, the original strict comparison changes only after the
        # adjacent binary32 value crosses the source threshold.
        self.assertFalse(_squat_rv_continue(0xBF200001, COMMON_THRESHOLD_BITS))
        self.assertFalse(_squat_rv_continue(0xBF200000, COMMON_THRESHOLD_BITS))
        self.assertTrue(_squat_rv_continue(0xBF1FFFFF, COMMON_THRESHOLD_BITS))

        # Around -0.58999997..., equality remains false and the next greater y is
        # true; the original comparison direction is unchanged.
        self.assertFalse(_squat_rv_continue(0xBF170A3E, OVERRIDE_THRESHOLD_BITS))
        self.assertFalse(_squat_rv_continue(0xBF170A3D, OVERRIDE_THRESHOLD_BITS))
        self.assertTrue(_squat_rv_continue(0xBF170A3C, OVERRIDE_THRESHOLD_BITS))


if __name__ == "__main__":
    unittest.main()
