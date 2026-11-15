"""Byte-for-byte comparison of the C codecs with EdgeWeave's Python codecs
(link/wire.py, link/message.py).  Both are written against PROTOCOL.md; any
difference here is either a bug on one side or an ambiguity in the text, and
goes into docs/draft-3-proposal.md."""
import random
import struct
import unittest

from interop import support

support.require_env()
from link import message as M  # noqa: E402
from link import wire  # noqa: E402

SEED = 20261115
N = 400


def hx(b):
    return b.hex() if b else "-"


def py_enc(fn, *a):
    try:
        return "ok " + hx(fn(*a))
    except wire.WireError as e:
        return "err " + e.code
    except M.MessageError:
        return "err MALFORMED"


class Cross(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c = support.CTool()
        cls.r = random.Random(SEED)

    @classmethod
    def tearDownClass(cls):
        cls.c.close()

    def same(self, c_line, py_line, what):
        self.assertEqual(c_line, py_line, what)

    def test_beacon(self):
        r = self.r
        for _ in range(N):
            v = r.choice([1, 1, 1, 1, 0, 2, 255])
            p = r.choice([0, 1, 2, 3, 3, 4, 127, 255])
            x = r.choice([0, 1, 0x1234, 0xFFFF, r.randrange(65536)])
            a = r.choice([0, 0, 1, 255])
            f = r.choice([0, 1, 2, 4, 7, 8, 0x80, 0xFF, r.randrange(256)])
            self.same(self.c.ask("beacon %d %d %d %d %d" % (v, p, x, a, f)),
                      py_enc(wire.beacon_encode, v, p, x, a, f), ("beacon", v, p, x, a, f))
        for _ in range(N):
            data = bytes(r.randrange(256) for _ in range(r.choice([0, 5, 6, 6, 6, 7])))
            if len(data) == 6 and r.random() < 0.8:
                data = bytes([r.choice([1, 1, 1, 2]), r.choice([0, 1, 2, 3, 4]), r.randrange(256),
                              r.randrange(256), r.randrange(256), r.choice([0, 1, 7, 0x80, r.randrange(256)])])
            try:
                d = wire.beacon_decode(data)
                want = "ok v=%d p=%d x=%d a=%d f=%d" % (d["version"], d["profile"], d["xid"], d["attempt"], d["flags"])
            except wire.WireError as e:
                want = "err " + e.code
            self.same(self.c.ask("beacon_d " + hx(data)), want, ("beacon_d", data.hex()))

    def test_frame(self):
        r = self.r
        for _ in range(N):
            seq = r.choice([0, 1, 0xFFFF, r.randrange(65536)])
            op = r.choice([0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13, 0x14, 0x7F, 0xFF, r.randrange(256)])
            pl = bytes(r.randrange(256) for _ in range(r.choice([0, 0, 1, 5, 40])))
            self.same(self.c.ask("frame %d %d %s" % (seq, op, hx(pl))),
                      py_enc(wire.frame_encode, seq, op, pl), ("frame", seq, op, pl.hex()))
        for _ in range(N):
            data = bytes(r.randrange(256) for _ in range(r.choice([0, 1, 2, 3, 3, 4, 9])))
            if len(data) >= 3 and r.random() < 0.7:
                data = data[:2] + bytes([r.choice([0, 1, 2, 3, 0x10, 0x13, 0x14])]) + data[3:]
            try:
                f = wire.frame_decode(data)
                want = "ok s=%d o=%d p=%s" % (f["seq"], f["opcode"], hx(f["payload"]))
            except wire.WireError as e:
                want = "err " + e.code
            self.same(self.c.ask("frame_d " + hx(data)), want, ("frame_d", data.hex()))

    def test_hello_accept(self):
        r = self.r
        nodes = [0, 1, 0x0102030405060708, 0xFFFFFFFFFFFFFFFF, r.getrandbits(64)]
        for _ in range(N):
            v = r.choice([1, 1, 1, 2])
            p = r.choice([0, 1, 2, 3, 4, 9])
            d = r.choice([0, 0, 1, 2])
            x = r.choice([0, 1, 0x1234, r.randrange(65536)])
            n = r.choice(nodes)
            m = r.choice([0, 1, 0x11, 0xFFFF, r.randrange(65536)])
            self.same(self.c.ask("hello %d %d %d %d %d %d" % (v, p, d, x, n, m)),
                      py_enc(wire.hello_encode, v, p, d, x, n, m), ("hello", v, p, d, x, n, m))
            self.same(self.c.ask("accept %d %d %d %d %d" % (v, d, x, n, m)),
                      py_enc(wire.accept_encode, v, d, x, n, m), ("accept", v, d, x, n, m))
        for _ in range(N):
            n = r.choice([14, 15, 13, 16, 0])
            data = bytes(r.randrange(256) for _ in range(n))
            if n >= 14 and r.random() < 0.8:
                data = bytes([1]) + data[1:]
            for cmd, dec, size in (("hello_d", wire.hello_decode, 15), ("accept_d", wire.accept_decode, 14)):
                try:
                    d = dec(data)
                    if cmd == "hello_d":
                        want = "ok v=%d p=%d d=%d x=%d n=%d m=%d" % (
                            d["version"], d["profile"], d["delivery"], d["xid"], d["node_id"], d["max_rx_payload"])
                    else:
                        want = "ok v=%d d=%d x=%d n=%d m=%d" % (
                            d["version"], d["delivery"], d["xid"], d["node_id"], d["max_rx_payload"])
                except wire.WireError as e:
                    want = "err " + e.code
                self.same(self.c.ask("%s %s" % (cmd, hx(data))), want, (cmd, data.hex()))

    # ---------------------------------------------------------------- Message

    def py_message(self, cmd, i, src, topic, lease, pol, st, payload):
        if cmd == 1:
            return M.encode_subscribe(i, src, topic, lease, pol)
        if cmd == 2:
            return M.encode_unsubscribe(i, src, topic)
        if cmd == 3:
            return M.encode_publish(i, topic, payload)
        if cmd == 4:
            return M.encode_deliver(i, src, topic, payload)
        if cmd == 5:
            return M.encode_ack(i, st)
        raise M.MessageError("unknown cmd")

    def test_message_encode(self):
        r = self.r
        for _ in range(N * 2):
            cmd = r.choice([0, 1, 2, 3, 4, 5, 5, 6, 255])
            i = r.choice([0, 1, 0xFFFF, r.randrange(65536)])
            src = r.choice([0, 1, 0x0102030405060708, 0xFFFFFFFFFFFFFFFF, r.getrandbits(64)])
            topic = r.choice([0, 1, 0x100, 0xFFFF, r.randrange(65536)])
            lease = r.choice([0, 60, 0xFFFF, r.randrange(65536)])
            pol = r.choice([0, 1, 2, 3, 255])
            st = r.choice([0, 1, 2, 3, 4, 5, 255])
            payload = bytes(r.randrange(256) for _ in range(r.choice([0, 0, 1, 2, 30, 200])))
            want = py_enc(self.py_message, cmd, i, src, topic, lease, pol, st, payload)
            got = self.c.ask("msg %d %d %d %d %d %d %d %s" % (cmd, i, src, topic, lease, pol, st, hx(payload)))
            self.same(got, want, ("msg", cmd, i, src, topic, lease, pol, st, payload.hex()))

    def py_msg_decode(self, data):
        try:
            d = M.decode(data)
        except M.MessageError:
            return None
        cmd = {"SUBSCRIBE": 1, "UNSUBSCRIBE": 2, "PUBLISH": 3, "DELIVER": 4, "ACK": 5}[d["cmd"]]
        pl = d.get("payload", b"")
        return "ok c=%d id=%d s=%d t=%d l=%d q=%d st=%d p=%s" % (
            cmd, d.get("request_id", d.get("message_id")), d.get("source_node_id", 0), d.get("topic", 0),
            d.get("lease_seconds", 0), d.get("queue_policy", 0), d.get("status", 0), hx(pl))

    def test_message_decode_including_mutations_and_splits(self):
        r = self.r
        seeds = [
            M.encode_subscribe(1, 0x0102030405060708, 0x100, 60, 1),
            M.encode_unsubscribe(1, 0x0102030405060708, 0x100),
            M.encode_publish(2, 0x100, b"\xde\xad"),
            M.encode_publish(2, 0x100, b""),
            M.encode_deliver(3, 0x1112131415161718, 0x100, b"\xbe\xef" * 20),
            M.encode_ack(2, 0),
        ]
        for _ in range(N * 2):
            data = bytearray(r.choice(seeds))
            m = r.random()
            if m < 0.25 and len(data):
                del data[r.randrange(len(data)):]               # truncate
            elif m < 0.4:
                data += bytes(r.randrange(256) for _ in range(r.randrange(1, 4)))   # extend
            elif m < 0.6 and len(data):
                data[r.randrange(len(data))] = r.choice([0, 1, 3, 5, 6, 0x7F, 0xFF, r.randrange(256)])
            data = bytes(data)
            want = self.py_msg_decode(data)
            got = self.c.ask("msg_d " + hx(data)) if data else "err MALFORMED"
            if want is None:
                self.assertTrue(got.startswith("err"), (data.hex(), got))
            else:
                self.assertEqual(got, want, data.hex())
            # the streaming parser must give the same answer for any fragmentation
            if data:
                step = r.choice([1, 2, 3, 5, 7])
                got2 = self.c.ask("msg_split %d %s" % (step, hx(data)))
                self.assertEqual(got2.startswith("err"), got.startswith("err"), (data.hex(), step))
                if not got.startswith("err"):
                    self.assertEqual(got2, got, (data.hex(), step))

    # ---------------------------------------------------------------- binding

    def test_stream_binding_record(self):
        r = self.r
        for _ in range(N):
            ch = r.choice([0, 1, 2])
            frame = wire.frame_encode(r.randrange(65536), r.choice(wire.KNOWN_OPCODES),
                                      bytes(r.randrange(256) for _ in range(r.choice([0, 1, 20, 300]))))
            record = struct.pack(">BH", ch, len(frame)) + frame      # link/session.py _write_record
            self.same(self.c.ask("record %d %s" % (ch, frame.hex())), "ok " + record.hex(), ("record", ch))
            self.assertEqual(self.c.ask("record_d " + record.hex()), "ok ch=%d f=%s" % (ch, frame.hex()))
        # the receiver's rejections (link/session.py read_record rules)
        self.assertEqual(self.c.ask("record_d 030003000013"), "err MALFORMED")      # channel 3
        self.assertEqual(self.c.ask("record_d 0100020000"), "err MALFORMED")        # length < 3
        self.assertEqual(self.c.ask("record_d 010005000013"), "err INCOMPLETE")  # short body


if __name__ == "__main__":
    unittest.main()
