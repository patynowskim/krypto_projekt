"""Protocol tests that do not need an ESP32."""

from __future__ import annotations

import unittest

import satlink as S


class SppTests(unittest.TestCase):
    def test_roundtrip_tm(self) -> None:
        h = S.SppPrimary(type=S.SPP_TYPE_TM, apid=S.APID_TM_HK, seq_count=42, data_len=25)
        back = S.SppPrimary.unpack(h.pack())
        self.assertEqual(back, h)

    def test_bit_layout_example(self) -> None:
        # version=0 type=TM SH=0 APID=1 flags=unseg seq=1 data_len=3 (4-byte user)
        h = S.SppPrimary(apid=1, seq_count=1, data_len=3)
        raw = h.pack()
        self.assertEqual(len(raw), 6)
        ident, seq, length = int.from_bytes(raw[0:2], "big"), int.from_bytes(raw[2:4], "big"), int.from_bytes(raw[4:6], "big")
        self.assertEqual(ident, 0x0001)
        self.assertEqual(seq, 0xC001)  # flags 11 | count 1
        self.assertEqual(length, 3)

    def test_idle_rejects_secondary_header(self) -> None:
        h = S.SppPrimary(apid=S.APID_IDLE, sec_hdr=1, data_len=0)
        with self.assertRaises(S.SatError):
            h.pack()


@unittest.skip("TODO: crypto layer not implemented yet")
class GcmNistTests(unittest.TestCase):
    def test_sp800_38d_aes256_all_zero(self) -> None:
        pass


@unittest.skip("TODO: crypto + anti-replay not implemented yet")
class FrameTests(unittest.TestCase):
    def test_seal_open_overhead(self) -> None:
        pass

    def test_bitflip_fails_auth(self) -> None:
        pass

    def test_replay_rejected(self) -> None:
        pass


if __name__ == "__main__":
    unittest.main()
