#!/usr/bin/env python3
"""Literal socket fixtures for the independent qualification client's credit lane."""
import socket
import struct
import unittest

from core_client import Client

HEADER = struct.Struct('!4sHHHHIQQ')


def frame(kind, request=0, session=7, body=b''):
    return HEADER.pack(b'AVCP', 1, kind, 0, 0, len(body), request, session) + body


def output(sequence, data):
    return frame(9, body=struct.pack('!Q', sequence) + data)


def ack(request, session=7, completed=14):
    return frame(13, request, session, struct.pack('!H', completed))


def error(request, code, session=7):
    return frame(12, request, session, struct.pack('!HIH', code, 0, 0))


class CreditTests(unittest.TestCase):
    def setUp(self):
        self.new_client()

    def new_client(self):
        self.endpoint, self.peer = socket.socketpair()
        self.endpoint.settimeout(.2)
        self.peer.settimeout(.2)
        self.addCleanup(self.endpoint.close)
        self.addCleanup(self.peer.close)
        self.client = Client.__new__(Client)
        self.client.socket = self.endpoint
        self.client.request = 10
        self.client.frames, self.client.outputs, self.client.sequence = [], {}, {}
        self.client.sessions = {7}
        self.client.terminal, self.client.closing, self.client.closed = set(), set(), set()
        self.client.pending, self.client.credit_pending, self.client.consumed = {}, {}, {}
        self.client.credit_completions = 0

    def receive(self):
        def exact(size):
            data = b''
            while len(data) < size:
                data += self.peer.recv(size - len(data))
            return data
        h = HEADER.unpack(exact(32))
        return h[2], h[6], h[7], exact(h[5])

    def no_request(self):
        self.peer.setblocking(False)
        try:
            with self.assertRaises(BlockingIOError):
                self.peer.recv(1)
        finally:
            self.peer.settimeout(.2)

    def begin_credit(self):
        self.peer.sendall(output(1, b'A'))
        self.client.read()
        self.assertEqual(self.receive(), (14, 11, 7, b'\x00\x00\x00\x01'))
        return 11

    def test_delayed_ack_coalesces_exact_balance_once(self):
        request = self.begin_credit()
        self.peer.sendall(output(2, b'BC') + output(3, b'DEF'))
        self.client.read()
        self.client.read()
        self.no_request()
        self.peer.sendall(ack(request))
        self.client.read()
        self.assertEqual(self.receive(), (14, 12, 7, b'\x00\x00\x00\x05'))
        self.peer.sendall(ack(12))
        self.client.read()
        self.no_request()
        self.assertEqual(self.client.credit_pending, {})

    def test_async_output_and_credit_complete_during_ordinary_wait(self):
        request = self.begin_credit()
        resize = self.client.send(6, 7, b'\x00\x18\x00\x50')
        self.assertEqual(self.receive()[:3], (6, resize, 7))
        self.peer.sendall(output(2, b'BC') + ack(request) + ack(resize, completed=6))
        self.assertEqual(self.client.await_request(resize), (13, resize, 7, b'\x00\x06'))
        self.assertEqual(self.receive(), (14, resize + 1, 7, b'\x00\x00\x00\x02'))
        self.assertEqual(self.client.outputs[7], b'ABC')

    def test_bad_credit_terminals_fail(self):
        for malformed in (error(11, 4), error(11, 3), ack(11, session=8),
                          ack(11, completed=6), frame(4, 11, 7, b''), ack(999), error(11, 3, session=8)):
            with self.subTest(malformed=malformed.hex()):
                # Each malformed reply uses a fresh client with a known Credit.
                self.new_client()
                self.begin_credit()
                self.peer.sendall(malformed)
                with self.assertRaises(AssertionError):
                    self.client.read()

    def test_duplicate_terminal_fails(self):
        request = self.begin_credit()
        self.peer.sendall(ack(request) + ack(request))
        self.client.read()
        with self.assertRaises(AssertionError):
            self.client.read()

    def test_credit_failure_during_ordinary_wait_fails(self):
        credit = self.begin_credit()
        resize = self.client.send(6, 7, b'\x00\x18\x00\x50')
        self.receive()
        self.peer.sendall(error(credit, 4) + ack(resize, completed=6))
        with self.assertRaises(AssertionError):
            self.client.await_request(resize)

    def test_closed_ack_resolves_pending_without_new_credit(self):
        credit = self.begin_credit()
        self.peer.sendall(output(2, b'BC'))
        self.client.read()
        self.no_request()
        close = self.client.send(7, 7)
        self.receive()
        self.peer.sendall(frame(11, close, body=struct.pack('!Q', 2)) + ack(credit))
        self.client.read()
        self.assertEqual(self.client.credit_pending, {7: credit})
        self.client.read()
        self.assertEqual(self.client.credit_pending, {})
        self.no_request()

    def test_pending_survives_closed_unknown_session_once(self):
        request = self.begin_credit()
        close = self.client.send(7, 7)
        self.assertEqual(self.receive()[:3], (7, close, 7))
        self.peer.sendall(frame(11, close, body=struct.pack('!Q', 1)))
        self.client.read()
        self.assertEqual(self.client.credit_pending, {7: request})
        self.peer.sendall(error(request, 3) + error(request, 3))
        self.client.read()
        self.assertEqual(self.client.credit_pending, {})
        with self.assertRaises(AssertionError):
            self.client.read()

    def test_shutdown_marks_created_session_closing_before_output(self):
        self.client.send(8)
        self.receive()
        self.peer.sendall(output(1, b'A'))
        self.client.read()
        self.assertEqual(self.receive()[:3], (14, 12, 7))
        self.peer.sendall(error(12, 3))
        self.client.read()
        self.assertEqual(self.client.credit_pending, {})

    def test_closing_unknown_session_allowed_but_state_fails(self):
        request = self.begin_credit()
        close = self.client.send(7, 7)
        self.receive()
        self.peer.sendall(error(request, 3))
        self.client.read()
        self.assertEqual(self.client.credit_pending, {})
        # A different error is not justified by the closing state.
        self.new_client()
        request = self.begin_credit()
        self.client.send(7, 7)
        self.receive()
        self.peer.sendall(error(request, 4))
        with self.assertRaises(AssertionError):
            self.client.read()


if __name__ == '__main__':
    unittest.main()
