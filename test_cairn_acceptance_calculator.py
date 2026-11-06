"""Unittest acceptance sample for cairn_acceptance_calculator.add."""

import unittest

from cairn_acceptance_calculator import add


class TestAdd(unittest.TestCase):
    def test_positive(self):
        self.assertEqual(add(3, 4), 7)

    def test_negative(self):
        self.assertEqual(add(-3, -4), -7)

    def test_zero(self):
        self.assertEqual(add(9, 0), 9)

    def test_mixed_sign(self):
        self.assertEqual(add(-3, 7), 4)


if __name__ == "__main__":
    unittest.main()
