#!/usr/bin/env python3
"""Derives pause_menu_achievements_as.h from pause_menu_as.h (as2c's output of pause_menu.as).

The Android build (no photo mode there) makes the START menu's own Achievements row (the movie's
Screenshots_mc: greyed out, as the console version shows no achievements here) work, and drops the
Photo Mode row. As pause_menu.as, with these changes:

  * ROWS has only the Cheats row (the Photo Mode element is cut out, the array's length is 1);
  * the game's row's init, which ended in this.Disable(), does instead
        RowInit({ label: "Achievements", message: "DAORecomp.Achievements" },
            MainMenu.SELECTABLE_PROFILE).call(this);
    (the label, the sound, the click that closes the menu and sends the message, Enable()).

Each change grows or shrinks the script, so the code sizes of the DefineFunction2 actions around it
are corrected, and no jump may cross a change (checked). Run from DAORecomp/ui:
    python3 make_pause_menu_achievements.py
"""
import re
import struct

SRC, DST, NAME = 'pause_menu_as.h', 'pause_menu_achievements_as.h', 'kPauseMenuAchievementsScript'


def load():
    text = open(SRC).read()
    m = re.search(r'kPauseMenuScript\[\] = \{(.*?)\};', text, re.S)
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1)))


def walk(b, start, end, out, funcs):
    """Actions as (offset, code, length); functions as (offset, size field offset, size)."""
    i = start
    while i < end:
        code = b[i]
        if code < 0x80:
            out.append((i, code, 0))
            i += 1
            continue
        length = struct.unpack_from('<H', b, i + 1)[0]
        out.append((i, code, length))
        body = i + 3
        if code == 0x8E:  # DefineFunction2: name, params, register count, flags, parameters, code size
            p = b.index(0, body) + 1
            n = struct.unpack_from('<H', b, p)[0]
            p += 2 + 1 + 2
            for _ in range(n):
                p = b.index(0, p + 1) + 1
            size = struct.unpack_from('<H', b, p)[0]
            funcs.append((i, p, size))
            i = body + length
            walk(b, i, i + size, out, funcs)
            i += size
            continue
        i = body + length


def analyse(b):
    out, funcs = [], []
    walk(b, 0, len(b), out, funcs)
    return out, funcs


def push_string(s):
    data = b'\x00' + s.encode() + b'\x00'
    return b'\x96' + struct.pack('<H', len(data)) + data


def push_int(n):
    return b'\x96\x05\x00\x07' + struct.pack('<i', n)


PUSH_THIS = b'\x96\x02\x00\x04\x01'  # register 1 (this) in the row's init function


def string_of(b, action):
    i, code, length = action
    if code == 0x96 and length >= 2 and b[i + 3] == 0 and b[i + 3 + length - 1] == 0 and b.count(b'\0', i + 4, i + 3 + length) == 1:
        return b[i + 4:i + 3 + length - 1].decode()
    return None


def replace(b, start, end, new):
    """b with [start, end) replaced; the code sizes of the functions around it corrected, and no
    jump allowed to cross it."""
    out, funcs = analyse(b)
    for i, code, length in out:
        if code in (0x99, 0x9D):
            target = i + 5 + struct.unpack_from('<h', b, i + 3)[0]
            lo, hi = min(i + 5, target), max(i + 5, target)
            assert not (lo < end and start < hi) or (lo >= end or hi <= start), 'a jump crosses the change'
    delta = len(new) - (end - start)
    b = bytearray(b)
    for at, size_at, size in funcs:
        body = size_at + 2
        if body <= start and end <= body + size:
            b[size_at:size_at + 2] = struct.pack('<H', size + delta)
    b[start:end] = new
    return bytes(b)


def main():
    script = load()
    original_length = len(script)

    # 1) The game's row (Screenshots_mc): RowInit(...).call(this) in place of this.Disable().
    out, _ = analyse(script)
    strings = [string_of(script, a) for a in out]
    inits = [k for k, s in enumerate(strings) if s == 'achievementsInit']
    assert len(inits) == 2, inits          # the local's name, then the use in the row's init
    k = inits[1]
    # ... Push 'achievementsInit', GetVariable, Push 'call', CallMethod, Pop, then this.Disable():
    # Push i0, Push reg1, Push 'Disable', CallMethod, Pop.
    assert strings[k + 2] == 'call' and out[k + 3][1] == 0x52 and out[k + 4][1] == 0x17
    assert strings[k + 7] == 'Disable' and out[k + 8][1] == 0x52 and out[k + 9][1] == 0x17
    start, end = out[k + 5][0], out[k + 9][0] + 1
    new = (PUSH_THIS + push_int(1)
           + push_string('MainMenu') + b'\x1C' + push_string('SELECTABLE_PROFILE') + b'\x4E'
           + push_string('label') + push_string('Achievements')
           + push_string('message') + push_string('DAORecomp.Achievements') + push_int(2) + b'\x43'
           + push_int(2) + push_string('RowInit') + b'\x3D'
           + push_string('call') + b'\x52' + b'\x17')
    script = replace(script, start, end, new)

    # 2) ROWS without the Photo Mode element (its pushes and InitObject), and its length 1.
    out, _ = analyse(script)
    strings = [string_of(script, a) for a in out]
    k = strings.index('PhotoMode_mc')
    assert strings[k - 1] == 'name'
    j = k
    while out[j][1] != 0x43:               # its InitObject
        j += 1
    assert out[j + 1][1] == 0x96 and script[out[j + 1][0]:out[j + 1][0] + 8] == push_int(2)
    start, end = out[k - 1][0], out[j + 1][0] + 8
    script = replace(script, start, end, push_int(1))

    # Check: the actions walk to the end, the outer function ends where the script's last action
    # does less the same change, and what is meant is in.
    out, funcs = analyse(script)
    assert max(i + (3 + n if c >= 0x80 else 1) for i, c, n in out if i < funcs[0][1] + 2 + funcs[0][2]) == funcs[0][1] + 2 + funcs[0][2]
    strings = [string_of(script, a) for a in out]
    for wanted in ('Achievements', 'DAORecomp.Achievements', 'Cheats_mc', 'SELECTABLE_PROFILE'):
        assert wanted in strings, wanted
    for gone in ('PhotoMode_mc', 'Photo Mode', 'DAORecomp.PhotoMode'):
        assert gone not in strings, gone
    lines = []
    for k in range(0, len(script), 16):
        lines.append('    ' + ', '.join('0x%02X' % x for x in script[k:k + 16]) + ',')
    open(DST, 'w').write(
        '// Generated by make_pause_menu_achievements.py from pause_menu_as.h; do not edit.\n'
        "// The START menu script with the game's own Achievements row working, and no Photo Mode\n"
        '// row (Android).\n'
        '#pragma once\n#include <cstdint>\n\n'
        'inline constexpr uint8_t %s[] = {\n%s\n};\n' % (NAME, '\n'.join(lines)))
    print(original_length, '->', len(script), 'bytes, written to', DST)


if __name__ == '__main__':
    main()
