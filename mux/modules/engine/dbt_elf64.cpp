/*! \file dbt_elf64.cpp
 * \brief Minimal ELF64 loader for RV64IMD binaries.
 *
 * Reference: ~/riscv/dbt/elf_loader.c (ELF32 version).
 */

#include "dbt_elf64.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ELF64 constants
//
static constexpr uint8_t  ELFMAG0     = 0x7F;
static constexpr uint8_t  ELFMAG1     = 'E';
static constexpr uint8_t  ELFMAG2     = 'L';
static constexpr uint8_t  ELFMAG3     = 'F';
static constexpr uint8_t  ELFCLASS64  = 2;
static constexpr uint8_t  ELFDATA2LSB = 1;
static constexpr uint16_t ET_EXEC     = 2;
static constexpr uint16_t EM_RISCV    = 243;
static constexpr uint32_t PT_LOAD     = 1;
static constexpr uint32_t EF_RISCV_RVC = 0x0001;

static constexpr size_t EI_NIDENT = 16;

// ELF64 header
//
struct Elf64_Ehdr {
    uint8_t  e_ident[EI_NIDENT];
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

// ELF64 program header
//
struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static constexpr size_t DEFAULT_MEMORY_SIZE = 32 * 1024 * 1024; // 32 MB

int rv64_load_elf(const char *filename, rv64_binary_t *bin) {
    memset(bin, 0, sizeof(*bin));

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "rv64-elf: cannot open '%s'\n", filename);
        return -1;
    }

    // Read ELF header
    //
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fprintf(stderr, "rv64-elf: cannot read ELF header\n");
        fclose(f);
        return -1;
    }

    // Validate magic
    //
    if (ehdr.e_ident[0] != ELFMAG0 || ehdr.e_ident[1] != ELFMAG1 ||
        ehdr.e_ident[2] != ELFMAG2 || ehdr.e_ident[3] != ELFMAG3) {
        fprintf(stderr, "rv64-elf: not an ELF file\n");
        fclose(f);
        return -1;
    }

    // Must be 64-bit little-endian RISC-V executable
    //
    if (ehdr.e_ident[4] != ELFCLASS64) {
        fprintf(stderr, "rv64-elf: not a 64-bit ELF (class=%d)\n", ehdr.e_ident[4]);
        fclose(f);
        return -1;
    }
    if (ehdr.e_ident[5] != ELFDATA2LSB) {
        fprintf(stderr, "rv64-elf: not little-endian\n");
        fclose(f);
        return -1;
    }
    if (ehdr.e_type != ET_EXEC) {
        fprintf(stderr, "rv64-elf: not an executable (type=%d)\n", ehdr.e_type);
        fclose(f);
        return -1;
    }
    if (ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "rv64-elf: not RISC-V (machine=%d)\n", ehdr.e_machine);
        fclose(f);
        return -1;
    }

    // Reject compressed instructions
    //
    if (ehdr.e_flags & EF_RISCV_RVC) {
        fprintf(stderr, "rv64-elf: compressed (RVC) instructions not supported\n");
        fclose(f);
        return -1;
    }

    bin->entry_point = ehdr.e_entry;

    // Allocate flat memory
    //
    bin->memory_size = DEFAULT_MEMORY_SIZE;
    bin->memory = static_cast<uint8_t *>(calloc(1, bin->memory_size));
    if (!bin->memory) {
        fprintf(stderr, "rv64-elf: cannot allocate %zu bytes\n", bin->memory_size);
        fclose(f);
        return -1;
    }

    // Load PT_LOAD segments
    //
    if (ehdr.e_phnum == 0) {
        fprintf(stderr, "rv64-elf: no program headers\n");
        rv64_free_binary(bin);
        fclose(f);
        return -1;
    }

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        fseek(f, static_cast<long>(ehdr.e_phoff + i * ehdr.e_phentsize), SEEK_SET);
        if (fread(&phdr, sizeof(phdr), 1, f) != 1) {
            fprintf(stderr, "rv64-elf: cannot read program header %d\n", i);
            rv64_free_binary(bin);
            fclose(f);
            return -1;
        }

        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        // Bounds check
        //
        if (phdr.p_vaddr + phdr.p_memsz > bin->memory_size) {
            fprintf(stderr, "rv64-elf: segment at 0x%llX + 0x%llX exceeds memory\n",
                    (unsigned long long)phdr.p_vaddr,
                    (unsigned long long)phdr.p_memsz);
            rv64_free_binary(bin);
            fclose(f);
            return -1;
        }

        // Load file contents
        //
        if (phdr.p_filesz > 0) {
            fseek(f, static_cast<long>(phdr.p_offset), SEEK_SET);
            if (fread(bin->memory + phdr.p_vaddr, 1,
                      static_cast<size_t>(phdr.p_filesz), f)
                != static_cast<size_t>(phdr.p_filesz)) {
                fprintf(stderr, "rv64-elf: cannot read segment data\n");
                rv64_free_binary(bin);
                fclose(f);
                return -1;
            }
        }
        // BSS is already zero (calloc)
    }

    // Stack near top of memory, 16-byte aligned
    //
    bin->stack_top = bin->memory_size - 16;

    fclose(f);
    return 0;
}

void rv64_free_binary(rv64_binary_t *bin) {
    free(bin->memory);
    bin->memory = nullptr;
}
