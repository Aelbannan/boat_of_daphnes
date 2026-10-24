# Converts a Japanese Wind Waker save (.gci, GZLJ01) to the US game's (GZLE01): the two versions
# share the save layout (zeldaret/tww's d_save.h differs only for the demo), but a Japanese name
# (Shift-JIS) has no glyphs in the US font, so each used file is renamed, and the checksums the game
# tests (m_Do_MemCardRWmng.cpp) are redone.
#   jp2us.py <in.gci> <out.gci> [--name Link]
# The card file: header block (banner, icon, comment), the save block twice, then 9 picture blocks.
# A save block: save count, version, 3 files of 0x768 bytes + a u64 checksum, then a u32 checksum
# of the block. In a file, the player's name (17 bytes) is at 0x157, and 0x168 and 0x179 hold two
# more names: the Japanese game leaves its default name there (Shift-JIS), the US game "Link".
import struct, sys

BLOCK = 0x2000
FILE_SIZE, FILES_AT = 0x768, 8
PLAYER_NAME_AT, NAME_AT = 0x157, (0x168, 0x179)


def file_checksum(data):
    c0 = sum(data) & 0xFFFFFFFF
    c1 = (-(sum(data) + len(data))) & 0xFFFFFFFF  # the sum of ~v, v promoted to int
    return c0 << 32 | c1


def block_checksum(block):
    words = struct.unpack('>%dH' % ((BLOCK - 4) // 2), block[:BLOCK - 4])
    return (sum(words) & 0xFFFF) << 16 | (sum(~w & 0xFFFF for w in words) & 0xFFFF)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    name = sys.argv[sys.argv.index('--name') + 1] if '--name' in sys.argv else 'Link'
    if '--name' in sys.argv:
        args.remove(name)
    src, dst = args
    gci = bytearray(open(src, 'rb').read())
    if gci[:4] != b'GZLJ':
        sys.exit(f'{src}: not a Japanese Wind Waker save ({bytes(gci[:6])})')
    gci[3] = ord('E')
    body = memoryview(gci)[0x40:]
    new_name = name.encode('ascii')[:16].ljust(17, b'\0')
    for copy in (1, 2):
        base = copy * BLOCK
        for f in range(3):
            at = base + FILES_AT + f * (FILE_SIZE + 8)
            data = body[at:at + FILE_SIZE]
            if struct.unpack('>Q', body[at + FILE_SIZE:at + FILE_SIZE + 8])[0] != file_checksum(data):
                continue  # not a valid file: left as it is
            for n in (PLAYER_NAME_AT,) + NAME_AT:  # Japanese names: the given one (US: "Link")
                if data[n] >= 0x80:
                    body[at + n:at + n + 17] = new_name
            body[at + FILE_SIZE:at + FILE_SIZE + 8] = struct.pack('>Q', file_checksum(body[at:at + FILE_SIZE]))
        body[base + BLOCK - 4:base + BLOCK] = struct.pack('>I', block_checksum(body[base:base + BLOCK]))
    open(dst, 'wb').write(gci)
    print(f'{dst}: GZLE01')


if __name__ == '__main__':
    main()
