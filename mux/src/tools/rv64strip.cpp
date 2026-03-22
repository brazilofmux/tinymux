/*! \file rv64strip.cpp
 * \brief Convert a RV64 ELF to the .rv64 blob format.
 *
 * Reads an ELF64 produced by riscv64-*-gcc, builds a flat memory
 * image of all allocatable sections (preserving the linker's virtual
 * address layout), collects global function symbols, and writes an
 * rv64_blob_header + flat image + entry table.
 *
 * Usage: rv64strip input.elf output.rv64
 *
 * Standalone — no TinyMUX dependencies, no autoconf.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "../../rv64/rv64blob.h"

/* ---------------------------------------------------------------
 * Minimal ELF64 structures.
 * --------------------------------------------------------------- */

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum;
    uint16_t e_shentsize, e_shnum, e_shstrndx;
};

struct Elf64_Shdr {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
};

struct Elf64_Sym {
    uint32_t st_name;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
};

static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_NOBITS = 8;
static constexpr uint64_t SHF_ALLOC  = 0x2;
static constexpr uint64_t SHF_WRITE  = 0x1;
static constexpr uint8_t  STB_GLOBAL = 1;
static constexpr uint8_t  STT_FUNC   = 2;

static uint8_t ELF64_ST_BIND(uint8_t i) { return i >> 4; }
static uint8_t ELF64_ST_TYPE(uint8_t i) { return i & 0xf; }

/* ---------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------- */

static bool read_file(const char *path, std::vector<uint8_t> &data) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    data.resize(static_cast<size_t>(sz));
    size_t rd = fread(data.data(), 1, data.size(), f);
    fclose(f);
    return rd == data.size();
}

/* ---------------------------------------------------------------
 * Build a flat memory image of all ALLOC sections.
 *
 * Scans all sections with SHF_ALLOC, finds the lowest and highest
 * virtual addresses, allocates a contiguous buffer, and copies each
 * section's contents at its correct offset.  NOBITS sections (BSS)
 * are zero-filled.  This preserves the linker's exact layout.
 *
 * Returns: flat image, base vaddr, and the boundary between
 * initialized data and BSS (for the loader to know how much to
 * zero-fill on each execution).
 * --------------------------------------------------------------- */

struct flat_image {
    std::vector<uint8_t> data;
    uint64_t base_vaddr;        // lowest alloc section address
    uint64_t total_size;        // base_vaddr to end of highest section
    uint64_t init_size;         // bytes of CONTENTS data (code+rodata+data)
    uint64_t bss_size;          // bytes to zero-fill after init_size
};

static bool build_flat_image(const std::vector<uint8_t> &elf,
                              const Elf64_Ehdr *eh,
                              flat_image &out) {
    if (eh->e_shoff == 0 || eh->e_shnum == 0) return false;

    const Elf64_Shdr *shstrtab = reinterpret_cast<const Elf64_Shdr *>(
        elf.data() + eh->e_shoff + eh->e_shstrndx * eh->e_shentsize);
    const char *strtab = reinterpret_cast<const char *>(
        elf.data() + shstrtab->sh_offset);

    // Find extent of all ALLOC sections.
    uint64_t lo = UINT64_MAX, hi = 0;
    uint64_t init_hi = 0;  // highest end of CONTENTS sections
    for (int i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + i * eh->e_shentsize);
        if (!(sh->sh_flags & SHF_ALLOC)) continue;
        if (sh->sh_size == 0) continue;
        if (sh->sh_addr < lo) lo = sh->sh_addr;
        uint64_t end = sh->sh_addr + sh->sh_size;
        if (end > hi) hi = end;
        if (sh->sh_type != SHT_NOBITS && end > init_hi) init_hi = end;
    }
    if (lo >= hi) return false;

    // Allocate flat image (zero-initialized).
    out.base_vaddr = lo;
    out.total_size = hi - lo;
    out.init_size = init_hi - lo;
    out.bss_size = hi - init_hi;
    out.data.resize(static_cast<size_t>(out.init_size), 0);

    // Copy each CONTENTS section at its correct offset.
    for (int i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + i * eh->e_shentsize);
        if (!(sh->sh_flags & SHF_ALLOC)) continue;
        if (sh->sh_size == 0) continue;
        if (sh->sh_type == SHT_NOBITS) continue;  // BSS — already zero
        uint64_t off = sh->sh_addr - lo;
        if (off + sh->sh_size <= out.init_size) {
            memcpy(out.data.data() + off,
                   elf.data() + sh->sh_offset, sh->sh_size);
        }
    }

    return true;
}

/* ---------------------------------------------------------------
 * Collect global function symbols.
 * --------------------------------------------------------------- */

struct func_sym {
    std::string name;
    uint64_t    value;
};

static void collect_functions(const std::vector<uint8_t> &elf,
                               const Elf64_Ehdr *eh,
                               uint64_t text_addr,
                               std::vector<func_sym> &funcs) {
    for (int i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + i * eh->e_shentsize);
        if (sh->sh_type != SHT_SYMTAB) continue;

        const Elf64_Shdr *str_sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + sh->sh_link * eh->e_shentsize);
        const char *symstr = reinterpret_cast<const char *>(
            elf.data() + str_sh->sh_offset);

        int nsyms = static_cast<int>(sh->sh_size / sh->sh_entsize);
        for (int j = 0; j < nsyms; j++) {
            const Elf64_Sym *sym = reinterpret_cast<const Elf64_Sym *>(
                elf.data() + sh->sh_offset + j * sh->sh_entsize);
            if (ELF64_ST_BIND(sym->st_info) != STB_GLOBAL) continue;
            if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) continue;
            if (sym->st_shndx == 0) continue;

            const char *name = symstr + sym->st_name;
            if (name[0] == '\0') continue;

            func_sym fs;
            fs.name = name;
            fs.value = sym->st_value - text_addr;
            funcs.push_back(fs);
        }
    }

    std::sort(funcs.begin(), funcs.end(),
              [](const func_sym &a, const func_sym &b) {
                  return a.value < b.value;
              });
}

/* ---------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: rv64strip input.elf output.rv64\n");
        return 1;
    }

    std::vector<uint8_t> elf;
    if (!read_file(argv[1], elf)) {
        fprintf(stderr, "rv64strip: cannot read '%s'\n", argv[1]);
        return 1;
    }
    if (elf.size() < sizeof(Elf64_Ehdr)) {
        fprintf(stderr, "rv64strip: file too small\n");
        return 1;
    }

    const Elf64_Ehdr *eh = reinterpret_cast<const Elf64_Ehdr *>(elf.data());
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E'
        || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        fprintf(stderr, "rv64strip: not an ELF file\n");
        return 1;
    }

    // Build flat memory image preserving ELF virtual address layout.
    flat_image img;
    if (!build_flat_image(elf, eh, img)) {
        fprintf(stderr, "rv64strip: no allocatable sections\n");
        return 1;
    }

    // Collect global function symbols (offsets relative to base_vaddr).
    std::vector<func_sym> funcs;
    collect_functions(elf, eh, img.base_vaddr, funcs);
    if (funcs.empty()) {
        fprintf(stderr, "rv64strip: no global functions found\n");
        return 1;
    }

    // Output layout: header + flat image (init only) + entry table.
    // BSS is not stored — the loader zero-fills bss_size bytes after
    // the initialized data.
    uint32_t hdr_size = static_cast<uint32_t>(sizeof(rv64_blob_header));
    uint32_t image_off = hdr_size;
    uint32_t image_sz = static_cast<uint32_t>(img.init_size);
    uint32_t entry_off = (image_off + image_sz + 7) & ~7u;
    uint32_t entry_count = static_cast<uint32_t>(funcs.size());

    // Build header.
    rv64_blob_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = RV64_BLOB_MAGIC;
    hdr.version = RV64_BLOB_VERSION;
    hdr.code_offset = image_off;     // flat image starts here
    hdr.code_size = image_sz;        // initialized portion of flat image
    hdr.rodata_offset = 0;           // v2: not used (part of flat image)
    hdr.rodata_size = 0;
    hdr.entry_offset = entry_off;
    hdr.entry_count = entry_count;
    hdr.data_offset = 0;             // v2: not used separately
    hdr.data_size = 0;
    hdr.bss_size = static_cast<uint32_t>(img.bss_size);
    hdr.reserved = 0;

    // Build entry table.
    std::vector<rv64_blob_entry> entries(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        memset(&entries[i], 0, sizeof(rv64_blob_entry));
        strncpy(entries[i].name, funcs[i].name.c_str(), RV64_ENTRY_NAME_LEN - 1);
        entries[i].code_off = static_cast<uint32_t>(funcs[i].value);
        entries[i].flags = 0;
    }

    // Write output.
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "rv64strip: cannot create '%s'\n", argv[2]);
        return 1;
    }

    fwrite(&hdr, sizeof(hdr), 1, out);
    fwrite(img.data.data(), img.data.size(), 1, out);

    // Padding before entry table.
    long cur = ftell(out);
    for (long i = cur; i < entry_off; i++) fputc(0, out);
    fwrite(entries.data(), sizeof(rv64_blob_entry), entry_count, out);
    fclose(out);

    // Report.
    printf("rv64strip: %s -> %s\n", argv[1], argv[2]);
    printf("  base:   0x%lx\n", (unsigned long)img.base_vaddr);
    printf("  image:  %u bytes (init) + %u bytes (bss) = %lu total\n",
           image_sz, (uint32_t)img.bss_size,
           (unsigned long)img.total_size);
    printf("  entries: %u\n", entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        printf("    [%u] %s @ 0x%x\n", i, entries[i].name, entries[i].code_off);
    }

    return 0;
}
