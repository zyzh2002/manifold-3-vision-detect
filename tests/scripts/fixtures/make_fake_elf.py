#!/usr/bin/env python3
"""Write a minimal ELF shared object with the given SONAME (test fixture).

Host-test fixture only: the checker's readelf-based ELF/SONAME verification
needs real, parseable ELF objects, and no cross compiler is guaranteed on the
test host. The crafted file carries a valid ELF64 header (ET_DYN,
EM_AARCH64 by default), one PT_LOAD, one PT_DYNAMIC, and
.dynamic/.dynstr/.shstrtab sections so readelf -h and readelf -d resolve
the SONAME.

Usage: make_fake_elf.py <out> <soname> [machine]
"""
import struct
import sys

EM_AARCH64 = 183


def make_elf(path, soname, machine=EM_AARCH64):
    name = b"\0" + soname.encode() + b"\0"
    dyn_off = 0x40 + 56 * 2
    strtab_off = dyn_off + 48
    shstr = b"\0.dynamic\0.dynstr\0.shstrtab\0"
    shstr_off = strtab_off + len(name)
    shoff = shstr_off + len(shstr)
    size = shoff + 4 * 64

    dyn = (
        struct.pack("<QQ", 5, strtab_off)  # DT_STRTAB
        + struct.pack("<QQ", 14, 1)  # DT_SONAME -> name[1]
        + struct.pack("<QQ", 0, 0)  # DT_NULL
    )
    phdr = struct.pack("<IIQQQQQQ", 1, 7, 0, 0, 0, size, size, 8)  # PT_LOAD RWX
    phdr += struct.pack(  # PT_DYNAMIC RW
        "<IIQQQQQQ", 2, 6, dyn_off, dyn_off, dyn_off, 48, 48, 8
    )
    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\0" * 8,
        3,  # ET_DYN
        machine,
        1,  # e_version
        0,  # e_entry
        0x40,  # e_phoff
        shoff,
        0,  # e_flags
        64,  # e_ehsize
        56,  # e_phentsize
        2,  # e_phnum
        64,  # e_shentsize
        4,  # e_shnum
        3,  # e_shstrndx
    )
    sh_null = struct.pack("<IIQQQQIIQQ", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    sh_dyn = struct.pack(  # .dynamic, SHT_DYNAMIC, sh_link -> .dynstr
        "<IIQQQQIIQQ", 1, 6, 3, dyn_off, dyn_off, 48, 2, 0, 8, 16
    )
    sh_str = struct.pack(  # .dynstr, SHT_STRTAB
        "<IIQQQQIIQQ", 10, 3, 2, strtab_off, strtab_off, len(name), 0, 0, 1, 0
    )
    sh_shstr = struct.pack(  # .shstrtab, SHT_STRTAB
        "<IIQQQQIIQQ", 18, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0
    )
    blob = ehdr + phdr + dyn + name + shstr + sh_null + sh_dyn + sh_str + sh_shstr
    assert len(blob) == size
    with open(path, "wb") as f:
        f.write(blob)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit("usage: make_fake_elf.py <out> <soname> [machine]")
    machine = int(sys.argv[3]) if len(sys.argv) == 4 else EM_AARCH64
    make_elf(sys.argv[1], sys.argv[2], machine)
