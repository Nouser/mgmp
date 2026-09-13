#!/usr/bin/env python3
"""rederive_sdl_slots.py -- find the SDL_DYNAPI jump-table slots for a new build.

Usage:  python tools/rederive_sdl_slots.py "C:\\...\\Mewgenics.exe"

The overlay does not hook SDL_GL_SwapWindow; it overwrites that function's slot
in SDL's dynamic-API jump table (see kRva_SdlSwapSlot in mgmp_addresses.h), and
it reads SDL_GetWindowSize / SDL_GetWindowSizeInPixels out of theirs. The table
moves with every build, and a stale slot RVA points at arbitrary .data -- on
one build it was the bytes of a string, the detour was written over it, and
nothing was intercepted.

How the table is found, from the file image alone:
  * every slot initially holds a DEFAULT stub, and each stub jumps back through
    ITS OWN slot (that is what forces SDL_InitDynamicAPI on first use), so a
    slot is recognisable by that self-reference;
  * the longest run of such slots in .data is the table;
  * SDL 3.2's SDL_dynapi_procs.h order is append-only across 3.2.x, so the
    index of a function in it is stable: SDL_GL_SwapWindow = 202,
    SDL_GetWindowSize = 558, SDL_GetWindowSizeInPixels = 559. Those three
    reproduce the original pin (0x012DE650 / 0x012DF170 / 0x012DF178) exactly
    from a table base of 0x012DE000, which is what makes them trustworthy.
Each derived slot is then checked: self-referencing stub, and at least one
`jmp cs:[slot]` thunk in .text.
"""
import re, struct, sys

IMAGEBASE = 0x140000000
PROCS = {  # index in SDL_dynapi_procs.h (SDL 3.2.x)
    "kRva_SdlSwapSlot":            ("SDL_GL_SwapWindow",         202),
    "kRva_SdlGetWindowSizeSlot":   ("SDL_GetWindowSize",         558),
    "kRva_SdlGetWindowSizePxSlot": ("SDL_GetWindowSizeInPixels", 559),
}

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    opt = struct.unpack_from("<H", data, e + 20)[0]
    secs = {}
    for i in range(nsec):
        off = e + 24 + opt + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode(errors="replace")
        vs, va, rs, rp = struct.unpack_from("<IIII", data, off + 8)
        secs[name] = (va, vs, rp, rs)

    def rva_to_off(rva):
        for va, vs, rp, rs in secs.values():
            if va <= rva < va + max(vs, rs):
                return rp + (rva - va)
        return None

    tva, tvs, trp, trs = secs[".text"]
    text = data[trp:trp + trs]
    lo, hi = IMAGEBASE + tva, IMAGEBASE + tva + tvs

    def self_ref(slot):
        off = rva_to_off(slot)
        if off is None: return False
        stub = struct.unpack_from("<Q", data, off)[0] - IMAGEBASE
        soff = rva_to_off(stub)
        if soff is None: return False
        code = data[soff:soff + 64]
        return any(stub + k + 4 + struct.unpack_from("<i", code, k)[0] == slot
                   for k in range(len(code) - 4))

    def thunks(slot):
        return sum(1 for m in re.finditer(rb"\xFF\x25", text)
                   if tva + m.start() + 6 + struct.unpack_from("<i", text, m.start() + 2)[0] == slot)

    # The table: longest run of consecutive .text pointers in .data whose
    # entries self-reference. The first ~15 entries (the varargs procs, which
    # have hand-written stubs) do not self-reference, so score the run as a
    # whole rather than requiring every entry to.
    dva, dvs, drp, drs = secs[".data"]
    buf = data[drp:drp + drs]
    best = None
    start = None; n = 0
    for i in range(0, len(buf) - 8, 8):
        v = struct.unpack_from("<Q", buf, i)[0]
        if lo <= v < hi:
            if start is None: start, n = i, 0
            n += 1
        else:
            if start is not None and n >= 300:
                base = dva + start
                score = sum(1 for k in range(n) if self_ref(base + k * 8))
                if best is None or score > best[0]: best = (score, base, n)
            start = None
    if not best:
        sys.exit("no jump table found -- is this Mewgenics.exe?")
    score, base, n = best
    print(f"jump table: rva 0x{base:08X}, {n} entries, {score} self-referencing")

    ok = True
    for const, (fn, idx) in PROCS.items():
        slot = base + idx * 8
        sr, th = self_ref(slot), thunks(slot)
        good = sr and th >= 1
        ok &= good
        print(f"{const:30s} = 0x{slot:08X};   // {fn} (index {idx})"
              f"   self-ref={'yes' if sr else 'NO'} thunks={th}{'' if good else '   <-- NOT TRUSTWORTHY'}")
    print("\nPaste the three constants into src/core/mgmp_addresses.h." if ok else
          "\nAt least one slot failed its check -- the SDL version may have changed "
          "its proc order; re-count the indices in SDL_dynapi_procs.h.")

if __name__ == "__main__":
    main()
