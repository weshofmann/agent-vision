#!/usr/bin/env python3
"""The restoration oracle tolerates only Darwin's transient PENDIN state."""
from copy import deepcopy
import termios
import unittest
from desktop_pty import restored_termios


class RestorationOracle(unittest.TestCase):
    def setUp(self):
        self.before = [11010, 3, 19200, 1483, 9600, 9600,
                       [bytes([n]) for n in range(20)]]

    def test_pending_input_state_does_not_change_configuration(self):
        actual = deepcopy(self.before)
        actual[3] |= termios.PENDIN
        self.assertTrue(restored_termios(actual, self.before))
        self.assertTrue(restored_termios(self.before, actual))
        self.assertTrue(restored_termios(self.before, self.before))

    def test_each_mode_flag_and_speed_change_is_rejected(self):
        for field in range(6):
            with self.subTest(field=field):
                actual = deepcopy(self.before)
                actual[field] ^= 1
                actual[3] |= termios.PENDIN
                self.assertFalse(restored_termios(actual, self.before))

    def test_each_control_character_change_is_rejected(self):
        for field in range(20):
            with self.subTest(field=field):
                actual = deepcopy(self.before)
                actual[6][field] = b'\xff'
                actual[3] |= termios.PENDIN
                self.assertFalse(restored_termios(actual, self.before))


if __name__ == '__main__':
    unittest.main()
