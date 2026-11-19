"""PC-side twin of firmware/main/{ccsds_spp,sdls_lite,sat_frame}.

Used for unit tests, golden vectors, and offline replay checks.
Wire format is identical to the ESP32 firmware.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

ESPNOW_MAX = 1470  # ESP-NOW v2 (ESP_NOW_MAX_DATA_LEN_V2)
SPP_PRIMARY_LEN = 6
SPP_VERSION = 0
SPP_TYPE_TM = 0
SPP_TYPE_TC = 1
SPP_SEQ_UNSEGMENTED = 3
SPP_SEQ_MOD = 16384

APID_TM_HK = 0x001
APID_TM_PING = 0x002
APID_TC = 0x010
APID_IDLE = 0x7FF

SPI_LEN = 2
IV_LEN = 12
TAG_LEN = 16
SPI_DEFAULT = 0x0001
REPLAY_WINDOW = 64
CRYPTO_OVERHEAD = SPP_PRIMARY_LEN + SPI_LEN + IV_LEN + TAG_LEN  # 36
USER_MAX = ESPNOW_MAX - CRYPTO_OVERHEAD  # 1434


class SatError(Exception):
    def __init__(self, code: str, msg: str = "") -> None:
        super().__init__(msg or code)
        self.code = code


@dataclass
class SppPrimary:
    version: int = SPP_VERSION
    type: int = SPP_TYPE_TM
    sec_hdr: int = 0
    apid: int = APID_TM_HK
    seq_flags: int = SPP_SEQ_UNSEGMENTED
    seq_count: int = 0
    data_len: int = 0  # N-1

    def pack(self) -> bytes:
        if self.version != SPP_VERSION:
            raise SatError("ARG", "bad version")
        if self.apid == APID_IDLE and self.sec_hdr != 0:
            raise SatError("ARG", "idle packet cannot have secondary header")
        ident = (
            ((self.version & 7) << 13)
            | ((self.type & 1) << 12)
            | ((self.sec_hdr & 1) << 11)
            | (self.apid & 0x7FF)
        )
        seq = ((self.seq_flags & 3) << 14) | (self.seq_count & 0x3FFF)
        return struct.pack(">HHH", ident, seq, self.data_len & 0xFFFF)

    @classmethod
    def unpack(cls, raw: bytes) -> "SppPrimary":
        if len(raw) < SPP_PRIMARY_LEN:
            raise SatError("LEN")
        ident, seq, data_len = struct.unpack(">HHH", raw[:6])
        return cls(
            version=(ident >> 13) & 7,
            type=(ident >> 12) & 1,
            sec_hdr=(ident >> 11) & 1,
            apid=ident & 0x7FF,
            seq_flags=(seq >> 14) & 3,
            seq_count=seq & 0x3FFF,
            data_len=data_len,
        )


def pack_iv(epoch: int, counter: int) -> bytes:
    return struct.pack(">IQ", epoch & 0xFFFFFFFF, counter & 0xFFFFFFFFFFFFFFFF)


def unpack_iv(iv: bytes) -> tuple[int, int]:
    epoch, counter = struct.unpack(">IQ", iv)
    return epoch, counter


def pack_spi(spi: int) -> bytes:
    return struct.pack(">H", spi & 0xFFFF)


class SecurityAssociation:
    def __init__(self, key: bytes, epoch: int = 1) -> None:
        if len(key) != 32:
            raise SatError("ARG", "AES-256 key must be 32 bytes")
        self.key = key
        self.tx_epoch = epoch
        self.tx_next = 0
        self.rx_epoch = 0
        self.rx_highest = 0
        self.rx_bitmap = 0
        self.rx_inited = False

    def next_iv(self) -> bytes:
        raise NotImplementedError("TODO: next_iv")

    def encrypt(self, iv: bytes, aad: bytes, pt: bytes) -> tuple[bytes, bytes]:
        raise NotImplementedError("TODO: AES-256-GCM encrypt")

    def decrypt(self, iv: bytes, aad: bytes, ct: bytes, tag: bytes) -> bytes:
        raise NotImplementedError("TODO: AES-256-GCM decrypt")

    def replay_too_old(self, iv: bytes) -> bool:
        raise NotImplementedError("TODO: anti-replay check")

    def replay_commit(self, iv: bytes) -> None:
        raise NotImplementedError("TODO: anti-replay commit")


def seal(
    sa: SecurityAssociation,
    user: bytes,
    *,
    pkt_type: int,
    apid: int,
    seq_count: int,
) -> bytes:
    if not (1 <= len(user) <= USER_MAX):
        raise SatError("LEN", f"user {len(user)} not in 1..{USER_MAX}")
    hdr = SppPrimary(
        type=pkt_type,
        apid=apid,
        seq_count=seq_count & 0x3FFF,
        data_len=len(user) - 1,
    )
    primary = hdr.pack()
    spi = pack_spi(SPI_DEFAULT)
    iv = sa.next_iv()
    aad = primary + spi + iv
    ct, tag = sa.encrypt(iv, aad, user)
    frame = aad + ct + tag
    assert len(frame) == CRYPTO_OVERHEAD + len(user)
    return frame


def open_frame(sa: SecurityAssociation, frame: bytes) -> tuple[SppPrimary, bytes]:
    if not (CRYPTO_OVERHEAD + 1 <= len(frame) <= ESPNOW_MAX):
        raise SatError("LEN")
    hdr = SppPrimary.unpack(frame[:6])
    spi = struct.unpack(">H", frame[6:8])[0]
    iv = frame[8:20]
    ct = frame[20:-TAG_LEN]
    tag = frame[-TAG_LEN:]
    if hdr.data_len + 1 != len(ct):
        raise SatError("LEN", "Packet Data Length mismatch")
    if spi != SPI_DEFAULT:
        raise SatError("SPI")
    if sa.replay_too_old(iv):
        raise SatError("REPLAY")
    user = sa.decrypt(iv, frame[:20], ct, tag)
    sa.replay_commit(iv)
    return hdr, user
