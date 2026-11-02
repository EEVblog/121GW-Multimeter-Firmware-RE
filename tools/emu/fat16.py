"""Builds a minimal MBR-partitioned FAT16 SD card image for emulator tests."""
import struct

SECTOR = 512


def build_fat16_image(files, total_sectors=32768, part_start=63):
    """files: dict of 8.3 name ('EEVBLOG.BIN') -> bytes. Returns a bytearray image."""
    img = bytearray(total_sectors * SECTOR)
    vol_sectors = total_sectors - part_start
    reserved = 1
    n_fats = 2
    root_entries = 512
    root_sectors = root_entries * 32 // SECTOR
    sec_per_clus = 1
    clusters = vol_sectors  # upper bound, refined below
    fat_sectors = (clusters * 2 + SECTOR - 1) // SECTOR
    data_start = reserved + n_fats * fat_sectors + root_sectors
    clusters = (vol_sectors - data_start) // sec_per_clus
    assert 4085 <= clusters < 65525, "not a FAT16 cluster count"

    # MBR with one FAT16 partition
    mbr = bytearray(SECTOR)
    entry = struct.pack("<B3sB3sII", 0x00, b"\x00\x02\x00", 0x06, b"\xFE\xFF\xFF",
                        part_start, vol_sectors)
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xAA"
    img[0:SECTOR] = mbr

    base = part_start * SECTOR
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, sec_per_clus, reserved, n_fats,
                     root_entries, vol_sectors if vol_sectors < 0x10000 else 0, 0xF8,
                     fat_sectors, 63, 255, part_start,
                     vol_sectors if vol_sectors >= 0x10000 else 0)
    bs[36] = 0x80
    bs[38] = 0x29
    struct.pack_into("<I", bs, 39, 0x12345678)
    bs[43:54] = b"121GW TEST "
    bs[54:62] = b"FAT16   "
    bs[510:512] = b"\x55\xAA"
    img[base:base + SECTOR] = bs

    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    root = bytearray(root_sectors * SECTOR)
    next_cluster = 2
    for idx, (name, data) in enumerate(files.items()):
        stem, _, ext = name.upper().partition(".")
        short = stem.ljust(8)[:8].encode() + ext.ljust(3)[:3].encode()
        n_clus = max(1, (len(data) + SECTOR * sec_per_clus - 1) // (SECTOR * sec_per_clus))
        first = next_cluster if data else 0
        for c in range(n_clus if data else 0):
            cl = next_cluster + c
            struct.pack_into("<H", fat, cl * 2, 0xFFFF if c == n_clus - 1 else cl + 1)
        struct.pack_into("<11sBBBHHHHHHHI", root, idx * 32, short, 0x20, 0, 0, 0, 0x4C49, 0,
                         0, 0, 0x4C49, first, len(data))
        off = base + (data_start + (next_cluster - 2) * sec_per_clus) * SECTOR
        img[off:off + len(data)] = data
        if data:
            next_cluster += n_clus

    for f in range(n_fats):
        off = base + (reserved + f * fat_sectors) * SECTOR
        img[off:off + len(fat)] = fat
    off = base + (reserved + n_fats * fat_sectors) * SECTOR
    img[off:off + len(root)] = root
    return img
