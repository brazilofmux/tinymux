/*! \file rv64strip.cpp
 * \brief Convert a RV64 ELF to the .rv64 blob format.
 *
 * Reads an ELF64 produced by riscv64-*-gcc, extracts the .text
 * and .rodata sections, collects global function symbols, and
 * writes an rv64_blob_header + code + rodata + entry table.
 *
 * Usage: rv64strip input.elf output.rv64
 *
 * The tool is intentionally standalone — no TinyMUX dependencies,
 * no autoconf.  It can be built with any C++11 compiler.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "../../rv64/rv64blob.h"

/* ---------------------------------------------------------------
 * Minimal ELF64 structures (avoid elf.h for portability).
 * --------------------------------------------------------------- */

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Elf64_Sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint8_t  STB_GLOBAL = 1;
static constexpr uint8_t  STT_FUNC   = 2;

static uint8_t ELF64_ST_BIND(uint8_t i) { return i >> 4; }
static uint8_t ELF64_ST_TYPE(uint8_t i) { return i & 0xf; }

/* ---------------------------------------------------------------
 * Read entire file into a vector.
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
 * Find a section by name.
 * --------------------------------------------------------------- */

struct section {
    uint64_t offset;
    uint64_t size;
    uint64_t addr;
};

static bool find_section(const std::vector<uint8_t> &elf,
                          const Elf64_Ehdr *eh,
                          const char *name,
                          section &out) {
    if (eh->e_shoff == 0 || eh->e_shnum == 0) return false;

    /* Section header string table. */
    const Elf64_Shdr *shstrtab = reinterpret_cast<const Elf64_Shdr *>(
        elf.data() + eh->e_shoff + eh->e_shstrndx * eh->e_shentsize);
    const char *strtab = reinterpret_cast<const char *>(
        elf.data() + shstrtab->sh_offset);

    for (int i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + i * eh->e_shentsize);
        if (strcmp(strtab + sh->sh_name, name) == 0) {
            out.offset = sh->sh_offset;
            out.size = sh->sh_size;
            out.addr = sh->sh_addr;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------
 * Collect global function symbols.
 * --------------------------------------------------------------- */

struct func_sym {
    std::string name;
    uint64_t    value;  /* address / offset within .text */
};

static void collect_functions(const std::vector<uint8_t> &elf,
                               const Elf64_Ehdr *eh,
                               const section &text,
                               std::vector<func_sym> &funcs) {
    for (int i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + i * eh->e_shentsize);
        if (sh->sh_type != SHT_SYMTAB) continue;

        /* Associated string table. */
        const Elf64_Shdr *str_sh = reinterpret_cast<const Elf64_Shdr *>(
            elf.data() + eh->e_shoff + sh->sh_link * eh->e_shentsize);
        const char *strtab = reinterpret_cast<const char *>(
            elf.data() + str_sh->sh_offset);

        int nsyms = static_cast<int>(sh->sh_size / sh->sh_entsize);
        for (int j = 0; j < nsyms; j++) {
            const Elf64_Sym *sym = reinterpret_cast<const Elf64_Sym *>(
                elf.data() + sh->sh_offset + j * sh->sh_entsize);

            if (ELF64_ST_BIND(sym->st_info) != STB_GLOBAL) continue;
            if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC) continue;
            if (sym->st_shndx == 0) continue;  /* undefined */

            const char *name = strtab + sym->st_name;
            if (name[0] == '\0') continue;

            func_sym fs;
            fs.name = name;
            /* Offset within code section. */
            fs.value = sym->st_value - text.addr;
            funcs.push_back(fs);
        }
    }

    /* Sort by offset for deterministic output. */
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

    /* Validate ELF magic. */
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E'
        || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        fprintf(stderr, "rv64strip: not an ELF file\n");
        return 1;
    }

    /* Find .text section. */
    section text = {};
    if (!find_section(elf, eh, ".text", text)) {
        fprintf(stderr, "rv64strip: no .text section\n");
        return 1;
    }

    /* Find .rodata section (optional). */
    section rodata = {};
    bool has_rodata = find_section(elf, eh, ".rodata", rodata);

    /* Collect global function symbols. */
    std::vector<func_sym> funcs;
    collect_functions(elf, eh, text, funcs);

    if (funcs.empty()) {
        fprintf(stderr, "rv64strip: no global functions found\n");
        return 1;
    }

    /* Compute output layout. */
    uint32_t hdr_size = static_cast<uint32_t>(sizeof(rv64_blob_header));
    uint32_t code_off = hdr_size;
    uint32_t code_sz = static_cast<uint32_t>(text.size);
    uint32_t rodata_off = 0;
    uint32_t rodata_sz = 0;
    if (has_rodata && rodata.size > 0) {
        rodata_off = code_off + code_sz;
        /* Align to 8 bytes. */
        rodata_off = (rodata_off + 7) & ~7u;
        rodata_sz = static_cast<uint32_t>(rodata.size);
    }
    uint32_t entry_off = (rodata_off > 0) ? rodata_off + rodata_sz : code_off + code_sz;
    entry_off = (entry_off + 7) & ~7u;
    uint32_t entry_count = static_cast<uint32_t>(funcs.size());

    /* Build header. */
    rv64_blob_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = RV64_BLOB_MAGIC;
    hdr.version = RV64_BLOB_VERSION;
    hdr.code_offset = code_off;
    hdr.code_size = code_sz;
    hdr.rodata_offset = rodata_off;
    hdr.rodata_size = rodata_sz;
    hdr.entry_offset = entry_off;
    hdr.entry_count = entry_count;

    /* Build entry table. */
    std::vector<rv64_blob_entry> entries(entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        memset(&entries[i], 0, sizeof(rv64_blob_entry));
        strncpy(entries[i].name, funcs[i].name.c_str(), RV64_ENTRY_NAME_LEN - 1);
        entries[i].code_off = static_cast<uint32_t>(funcs[i].value);
        entries[i].flags = 0;
    }

    /* Write output. */
    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "rv64strip: cannot create '%s'\n", argv[2]);
        return 1;
    }

    fwrite(&hdr, sizeof(hdr), 1, out);

    /* Code section. */
    fwrite(elf.data() + text.offset, text.size, 1, out);

    /* Padding before rodata. */
    if (rodata_off > 0) {
        uint32_t pad = rodata_off - (code_off + code_sz);
        for (uint32_t i = 0; i < pad; i++) fputc(0, out);
        fwrite(elf.data() + rodata.offset, rodata.size, 1, out);
    }

    /* Padding before entry table. */
    long cur = ftell(out);
    uint32_t pad2 = entry_off - static_cast<uint32_t>(cur);
    for (uint32_t i = 0; i < pad2; i++) fputc(0, out);

    /* Entry table. */
    fwrite(entries.data(), sizeof(rv64_blob_entry), entry_count, out);

    fclose(out);

    /* Report. */
    printf("rv64strip: %s → %s\n", argv[1], argv[2]);
    printf("  code:   %u bytes at offset %u\n", code_sz, code_off);
    if (rodata_sz > 0) {
        printf("  rodata: %u bytes at offset %u\n", rodata_sz, rodata_off);
    }
    printf("  entries: %u\n", entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        printf("    [%u] %s @ 0x%x\n", i, entries[i].name, entries[i].code_off);
    }

    return 0;
}
