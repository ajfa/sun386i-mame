#!/usr/bin/env python3
"""The stub's 32-bit entry code, assembled by hand.

It builds a page directory and three page tables at a base the loader chooses
(the kernel and the standalone programs occupy different parts of low memory),
points CR3 at it, and jumps to the entry the loader left behind. Paging is only
switched on for the standalone programs: the kernel turns it on itself.
"""
import struct


def build(base_rom, entry_slot, paging_slot, table_slot, idtr_addr,
          scc_virt, scc_phys, pd_virt, scc_table):
    def d(v):
        return struct.pack("<I", v)

    high_index = lambda va: ((va - 0xffc00000) >> 12) * 4

    paging_on = bytearray()
    # the SCC and the page directory as the PROM leaves them for standalones
    paging_on += bytes([0xc7, 0x85]) + d(0x3000 + high_index(scc_virt)) + d(scc_phys | 3)
    paging_on += bytes([0x8d, 0x45, 0x03])                      # lea eax, [ebp+3]
    paging_on += bytes([0x89, 0x85]) + d(0x3000 + high_index(pd_virt))
    paging_on += bytes([0x0f, 0x01, 0x1d]) + d(idtr_addr)       # lidt [IDTR]
    paging_on += bytes([0x0f, 0x20, 0xc0])                      # mov eax, cr0
    paging_on += bytes([0x0d, 0x00, 0x00, 0x00, 0x80])          # or eax, 0x80000000
    paging_on += bytes([0x0f, 0x22, 0xc0])                      # mov cr0, eax
    paging_on += bytes([0xeb, 0x00])                            # jmp $+2

    pm = bytearray()
    pm += bytes([0xb8, 0x10, 0x00, 0x00, 0x00])                 # mov eax, 0x10
    for sreg in (0xd8, 0xc0, 0xd0, 0xe0, 0xe8):                 # ds es ss fs gs
        pm += bytes([0x8e, sreg])
    pm += bytes([0xbc, 0x00, 0x68, 0x00, 0x00])                 # mov esp, 0x6800

    # leave every serial line at the speed its driver expects to read back
    body = bytearray()
    body += bytes([0xad])                                       # lodsd, port
    body += bytes([0x85, 0xc0])                                 # test eax, eax
    body += bytes([0x74, 0x0f])                                 # je past the body
    body += bytes([0x89, 0xc2])                                 # mov edx, eax
    body += bytes([0xad])                                       # lodsd, time constant
    body += bytes([0xc6, 0x02, 0x0c])                           # mov [edx], 12
    body += bytes([0x88, 0x02])                                 # mov [edx], al
    body += bytes([0xc6, 0x02, 0x0d])                           # mov [edx], 13
    body += bytes([0x88, 0x22])                                 # mov [edx], ah
    body += bytes([0xeb, 0xec])                                 # jmp back
    pm += bytes([0xbe]) + d(scc_table)                          # mov esi, table
    pm += bytes(body)

    pm += bytes([0x8b, 0x2d]) + d(table_slot)                   # mov ebp, [table_slot]

    pm += bytes([0x89, 0xef])                                   # mov edi, ebp
    pm += bytes([0x31, 0xc0])                                   # xor eax, eax
    pm += bytes([0xb9, 0x00, 0x10, 0x00, 0x00])                 # mov ecx, 0x1000
    pm += bytes([0xf3, 0xab])                                   # rep stosd

    pm += bytes([0x8d, 0xbd]) + d(0x1000)                       # lea edi, [ebp+0x1000]
    pm += bytes([0xb8, 0x03, 0x00, 0x00, 0x00])                 # mov eax, 3
    pm += bytes([0xb9, 0x00, 0x08, 0x00, 0x00])                 # mov ecx, 0x800, 8 MiB
    pm += bytes([0xab, 0x05, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xf8])

    pm += bytes([0x8d, 0x85]) + d(0x1003)                       # lea eax, [ebp+0x1003]
    pm += bytes([0x89, 0x85]) + d(0x0000)                       # mov [ebp+0], eax
    pm += bytes([0x8d, 0x85]) + d(0x2003)
    pm += bytes([0x89, 0x85]) + d(0x0004)

    pm += bytes([0x8d, 0xbd]) + d(0x3000 + 0x3e0 * 4)           # lea edi, [ebp+...]
    pm += bytes([0xb8]) + d(base_rom | 3)
    pm += bytes([0xb9, 0x20, 0x00, 0x00, 0x00])
    pm += bytes([0xab, 0x05, 0x00, 0x10, 0x00, 0x00, 0xe2, 0xf8])

    pm += bytes([0x8d, 0x85]) + d(0x3003)
    pm += bytes([0x89, 0x85]) + d(0x0ffc)                       # PDE 0x3ff
    pm += bytes([0x0f, 0x22, 0xdd])                             # mov cr3, ebp

    pm += bytes([0x83, 0x3d]) + d(paging_slot) + bytes([0x00])  # cmp [paging_slot], 0
    pm += bytes([0x74, len(paging_on)])
    pm += paging_on

    pm += bytes([0x31, 0xd2, 0x31, 0xdb])                       # xor edx, edx; xor ebx, ebx
    pm += bytes([0xa1]) + d(entry_slot)                         # mov eax, [entry_slot]
    pm += bytes([0xff, 0xe0])                                   # jmp eax
    return bytes(pm)
