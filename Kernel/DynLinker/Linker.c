#include <AllTypes.h>
#include <KHeap.h>
#include <KMods.h>
#include <KrnPrintf.h>
#include <ModELF.h>
#include <ModMemMgr.h>
#include <String.h>
#include <VFS.h>

/**
 * @brief Install an ELF module
 *
 * @details Loads an ELF object file from the given path, allocates and maps all
 * 			sections, resolves symbols, applies relocations, and calls the module_init
 * 			entry point. On success the module remains resident; transient buffers are
 * 			freed. On failure, allocated memory is released.
 *
 * @param __Path__ Path to the ELF module file
 * @return 0 on success, -1 on failure
 */
int
InstallModule(const char* __Path__)
{
    if (!__Path__)
    {
        PError("MOD: Invalid path (NULL)\n");
        return -1;
    }

    static uint8_t ZeroStub[1] = {0};

    Elf64_Ehdr Hdr;
    long       HdrLen = 0;

    if (VfsReadAll(__Path__, &Hdr, (long)sizeof(Hdr), &HdrLen) != 0 || HdrLen < (long)sizeof(Hdr))
    {
        PError("MOD: Failed to read ELF header\n");
        return -1;
    }

    if (Hdr.e_ident[0] != 0x7F || Hdr.e_ident[1] != 'E' || Hdr.e_ident[2] != 'L' ||
        Hdr.e_ident[3] != 'F')
    {
        PError("MOD: Bad ELF magic\n");
        return -1;
    }

    if (Hdr.e_ident[4] != 2)
    {
        PError("MOD: Not ELF64\n");
        return -1;
    }

    if (Hdr.e_machine != 0x3E)
    {
        PError("MOD: Not x86-64\n");
        return -1;
    }

    if (Hdr.e_type != 1 && Hdr.e_type != 3)
    {
        PError("MOD: Unsupported ELF type\n");
        return -1;
    }

    PInfo("ELF: Header valid\n");

    long ShNum = (long)Hdr.e_shnum;
    if (ShNum <= 0)
    {
        PError("MOD: e_shnum=0\n");
        return -1;
    }

    long        ShtBytes = ShNum * (long)sizeof(Elf64_Shdr);
    Elf64_Shdr* ShTbl    = (Elf64_Shdr*)KMalloc((size_t)ShtBytes);
    if (!ShTbl)
    {
        PError("MOD: KMalloc ShTbl failed\n");
        return -1;
    }

    {
        File* F = VfsOpen(__Path__, VFlgRDONLY);
        if (!F)
        {
            KFree(ShTbl);
            PError("MOD: VfsOpen failed\n");
            return -1;
        }
        if (VfsLseek(F, (long)Hdr.e_shoff, VSeekSET) < 0)
        {
            VfsClose(F);
            KFree(ShTbl);
            PError("MOD: Seek SHT failed\n");
            return -1;
        }
        long Rd = VfsRead(F, ShTbl, ShtBytes);
        VfsClose(F);
        if (Rd < ShtBytes)
        {
            KFree(ShTbl);
            PError("MOD: SHT read failed\n");
            return -1;
        }
    }

    long SymtabIdx = -1, StrtabIdx = -1;
    for (long I = 0; I < ShNum; I++)
    {
        uint32_t T = ShTbl[I].sh_type;
        if (T == (uint32_t)2U && SymtabIdx < 0)
        {
            SymtabIdx = I;
        }
        else if (T == (uint32_t)3U && StrtabIdx < 0)
        {
            StrtabIdx = I;
        }
    }
    if (SymtabIdx < 0 || StrtabIdx < 0)
    {
        KFree(ShTbl);
        PError("MOD: Missing SHT_SYMTAB or SHT_STRTAB\n");
        return -1;
    }

    const Elf64_Shdr* SymSh = &ShTbl[SymtabIdx];
    const Elf64_Shdr* StrSh = &ShTbl[StrtabIdx];

    Elf64_Sym* SymBuf = (Elf64_Sym*)KMalloc((size_t)SymSh->sh_size);
    char*      StrBuf = (char*)KMalloc((size_t)StrSh->sh_size);
    if (!SymBuf || !StrBuf)
    {
        if (SymBuf)
        {
            KFree(SymBuf);
        }
        if (StrBuf)
        {
            KFree(StrBuf);
        }
        KFree(ShTbl);
        PError("MOD: KMalloc sym/str failed\n");
        return -1;
    }

    {
        File* F = VfsOpen(__Path__, VFlgRDONLY);
        if (!F)
        {
            KFree(SymBuf);
            KFree(StrBuf);
            KFree(ShTbl);
            PError("MOD: VfsOpen sym/str failed\n");
            return -1;
        }
        if (VfsLseek(F, (long)SymSh->sh_offset, VSeekSET) < 0)
        {
            VfsClose(F);
            KFree(SymBuf);
            KFree(StrBuf);
            KFree(ShTbl);
            PError("MOD: Seek .symtab failed\n");
            return -1;
        }
        long RdS = VfsRead(F, SymBuf, (long)SymSh->sh_size);
        if (VfsLseek(F, (long)StrSh->sh_offset, VSeekSET) < 0)
        {
            VfsClose(F);
            KFree(SymBuf);
            KFree(StrBuf);
            KFree(ShTbl);
            PError("MOD: Seek .strtab failed\n");
            return -1;
        }
        long RdT = VfsRead(F, StrBuf, (long)StrSh->sh_size);
        VfsClose(F);
        if (RdS < (long)SymSh->sh_size || RdT < (long)StrSh->sh_size)
        {
            KFree(SymBuf);
            KFree(StrBuf);
            KFree(ShTbl);
            PError("MOD: sym/str read short\n");
            return -1;
        }
    }

    long           SymCount = (long)((long)SymSh->sh_size / (long)sizeof(Elf64_Sym));
    __ElfSymbol__* Syms = (__ElfSymbol__*)KMalloc((size_t)(SymCount * (long)sizeof(__ElfSymbol__)));
    if (!Syms)
    {
        KFree(SymBuf);
        KFree(StrBuf);
        KFree(ShTbl);
        PError("MOD: KMalloc Syms failed\n");
        return -1;
    }

    for (long I = 0; I < SymCount; I++)
    {
        uint32_t    NameOff = SymBuf[I].st_name;
        const char* Nm      = (NameOff < (uint32_t)StrSh->sh_size) ? (StrBuf + NameOff) : 0;

        Syms[I].Name         = Nm;
        Syms[I].Value        = SymBuf[I].st_value;
        Syms[I].Shndx        = SymBuf[I].st_shndx;
        Syms[I].Info         = SymBuf[I].st_info;
        Syms[I].ResolvedAddr = 0ULL;
    }
    PInfo("ELF: Loaded symbols\n");

    void** SectionBases = (void**)KMalloc((size_t)(ShNum * (long)sizeof(void*)));
    if (!SectionBases)
    {
        KFree(Syms);
        KFree(SymBuf);
        KFree(StrBuf);
        KFree(ShTbl);
        PError("MOD: KMalloc SectionBases failed\n");
        return -1;
    }
    memset(SectionBases, 0, (size_t)(ShNum * (long)sizeof(void*)));

    for (long I = 0; I < ShNum; I++)
    {
        const Elf64_Shdr* S     = &ShTbl[I];
        long              Size  = (long)S->sh_size;
        uint32_t          Type  = S->sh_type;
        uint64_t          Flags = S->sh_flags;

        if (Size <= 0)
        {
            SectionBases[I] = (void*)ZeroStub;
            continue;
        }

        int   IsText = (Flags & (uint64_t)0x4ULL) ? 1 : 0;
        void* Base   = ModMalloc((size_t)Size, IsText ? 1 : 0);
        if (!Base)
        {
            PError("MOD: ModMalloc failed\n");
            for (long J = 0; J < ShNum; J++)
            {
                if (SectionBases[J] && SectionBases[J] != (void*)ZeroStub)
                {
                    long SzJ = (long)ShTbl[J].sh_size;
                    if (SzJ > 0)
                    {
                        ModFree(SectionBases[J], (size_t)SzJ);
                    }
                }
            }
            KFree(SectionBases);
            KFree(Syms);
            KFree(SymBuf);
            KFree(StrBuf);
            KFree(ShTbl);
            return -1;
        }

        SectionBases[I] = Base;

        if (Type == (uint32_t)8U)
        {
            memset(Base, 0, (size_t)Size);
        }
        else
        {
            File* F = VfsOpen(__Path__, VFlgRDONLY);
            if (!F)
            {
                PError("MOD: VfsOpen payload failed\n");
                return -1;
            }
            if (VfsLseek(F, (long)S->sh_offset, VSeekSET) < 0)
            {
                VfsClose(F);
                PError("MOD: Seek section failed\n");
                return -1;
            }
            long Rd = VfsRead(F, Base, Size);
            VfsClose(F);
            if (Rd < Size)
            {
                PError("MOD: Read section short\n");
                return -1;
            }
        }
    }

    for (long I = 0; I < SymCount; I++)
    {
        uint16_t Sh   = Syms[I].Shndx;
        uint64_t Base = 0;

        if (Sh == 0)
        {
            Base = 0;
        }
        else if (Sh < (uint16_t)ShNum)
        {
            Base = (uint64_t)SectionBases[Sh];
        }

        Syms[I].ResolvedAddr = Base ? (Base + Syms[I].Value) : 0ULL;
    }

    typedef struct
    {
        uint64_t r_offset;
        uint64_t r_info;
    } Elf64_Rel;

    for (long RIdx = 0; RIdx < ShNum; RIdx++)
    {
        const Elf64_Shdr* RelSh      = &ShTbl[RIdx];
        uint64_t          RelSecType = RelSh->sh_type;

        if (RelSecType != (uint64_t)4ULL && RelSecType != (uint64_t)9ULL)
        {
            continue;
        }

        uint32_t TgtIdx = RelSh->sh_info;
        if (TgtIdx >= (uint32_t)ShNum)
        {
            PWarn("ELF: RELOC invalid target\n");
            continue;
        }

        if (!SectionBases[TgtIdx])
        {
            SectionBases[TgtIdx] = (void*)ZeroStub;
        }

        long EntSz =
            (RelSecType == (uint64_t)4ULL) ? (long)sizeof(Elf64_Rela) : (long)sizeof(Elf64_Rel);
        long RelCnt = (long)((long)RelSh->sh_size / EntSz);
        if (RelCnt <= 0)
        {
            continue;
        }

        void* RelBuf = KMalloc((size_t)RelSh->sh_size);
        if (!RelBuf)
        {
            PError("ELF: KMalloc RELOC buf failed\n");
            continue;
        }

        File* RF = VfsOpen(__Path__, VFlgRDONLY);
        if (!RF)
        {
            KFree(RelBuf);
            PError("ELF: VfsOpen RELOC failed\n");
            continue;
        }
        if (VfsLseek(RF, (long)RelSh->sh_offset, VSeekSET) < 0)
        {
            VfsClose(RF);
            KFree(RelBuf);
            PError("ELF: Seek RELOC failed\n");
            continue;
        }
        long RdRel = VfsRead(RF, RelBuf, (long)RelSh->sh_size);
        VfsClose(RF);
        if (RdRel < (long)RelSh->sh_size)
        {
            KFree(RelBuf);
            PError("ELF: RELOC read short\n");
            continue;
        }

        for (long I = 0; I < RelCnt; I++)
        {
            uint32_t Type, SymIndex;
            uint64_t A;
            uint8_t* Loc;
            if (RelSecType == (uint64_t)4ULL)
            {
                const Elf64_Rela* R = &((Elf64_Rela*)RelBuf)[I];
                Type                = (uint32_t)(R->r_info & 0xffffffffU);
                SymIndex            = (uint32_t)(R->r_info >> 32);
                A                   = R->r_addend;
                Loc                 = ((uint8_t*)SectionBases[TgtIdx]) + R->r_offset;
            }
            else
            {
                const Elf64_Rel* R = &((Elf64_Rel*)RelBuf)[I];
                Type               = (uint32_t)(R->r_info & 0xffffffffU);
                SymIndex           = (uint32_t)(R->r_info >> 32);
                Loc                = ((uint8_t*)SectionBases[TgtIdx]) + R->r_offset;
                if (Type == 1U || Type == 8U)
                {
                    A = *(uint64_t*)Loc;
                }
                else if (Type == 2U || Type == 4U || Type == 10U || Type == 11U)
                {
                    A = (uint64_t)*(int32_t*)Loc;
                }
                else
                {
                    A = 0;
                }
            }

            if (SymIndex >= (uint32_t)SymCount)
            {
                PError("ELF: RELOC sym out of range\n");
                continue;
            }

            const __ElfSymbol__* Sym = &Syms[SymIndex];
            uint64_t             S   = Sym->ResolvedAddr;

            if (!S && Sym->Shndx == 0)
            {
                void* Ext = KexpLookup(Sym->Name);
                S         = (uint64_t)Ext;
                if (!Ext)
                {
                    PError("ELF: Undefined external symbol\n");
                    continue;
                }
            }

            switch (Type)
            {
                case 1U:
                    *(uint64_t*)Loc = S + A;
                    break;

                case 2U:
                case 4U:
                {
                    int64_t P      = (int64_t)((uint64_t)Loc + 4);
                    int64_t Disp64 = (int64_t)S - P;
                    *(int32_t*)Loc = (int32_t)Disp64;
                    break;
                }

                case 8U:
                    *(uint64_t*)Loc = (uint64_t)SectionBases[TgtIdx] + A;
                    break;

                case 9U:
                {
                    int64_t  P       = (int64_t)((uint64_t)Loc + 4);
                    __int128 S128    = (__int128)((int64_t)S);
                    __int128 A128    = (__int128)((int64_t)A);
                    __int128 P128    = (__int128)P;
                    __int128 Disp128 = S128 + A128 - P128;
                    int32_t  Disp32  = (int32_t)Disp128;
                    *(int32_t*)Loc   = Disp32;
                    break;
                }

                case 10U:
                {
                    uint64_t Val    = S + A;
                    *(uint32_t*)Loc = (uint32_t)Val;
                    break;
                }

                case 11U:
                {
                    int64_t Val    = (int64_t)(S + A);
                    *(int32_t*)Loc = (int32_t)Val;
                    break;
                }

                default:
                    PWarn("ELF: RELOC unsupported type\n");
                    break;
            }
        }

        KFree(RelBuf);
    }
    const __ElfSymbol__* InitSym = 0;
    const __ElfSymbol__* ExitSym = 0;

    for (long I = 0; I < SymCount; I++)
    {
        if (Syms[I].Name && strcmp(Syms[I].Name, "module_init") == 0)
        {
            InitSym = &Syms[I];
        }

        else if (Syms[I].Name && strcmp(Syms[I].Name, "module_exit") == 0)
        {
            ExitSym = &Syms[I];
        }
    }
    if (!InitSym)
    {
        PError("MOD: module_init not found\n");
        for (long J = 0; J < ShNum; J++)
        {
            if (SectionBases[J] && SectionBases[J] != (void*)ZeroStub)
            {
                long SzJ = (long)ShTbl[J].sh_size;
                if (SzJ > 0)
                {
                    ModFree(SectionBases[J], (size_t)SzJ);
                }
            }
        }
        KFree(SectionBases);
        KFree(Syms);
        KFree(StrBuf);
        KFree(SymBuf);
        KFree(ShTbl);
        return -1;
    }

    void (*InitFn)(void) = 0;
    void (*ExitFn)(void) = 0;

    if (InitSym->ResolvedAddr)
    {
        InitFn = (void (*)(void))(uintptr_t)InitSym->ResolvedAddr;
    }
    else
    {
        uint8_t* Base =
            (uint8_t*)((InitSym->Shndx < (uint16_t)ShNum) ? SectionBases[InitSym->Shndx] : 0);

        InitFn = (void (*)(void))(Base ? (Base + InitSym->Value) : (uint8_t*)InitSym->Value);
    }

    if (ExitSym)
    {
        if (ExitSym->ResolvedAddr)
        {
            ExitFn = (void (*)(void))(uintptr_t)ExitSym->ResolvedAddr;
        }
        else
        {
            uint8_t* BaseE =
                (uint8_t*)((ExitSym->Shndx < (uint16_t)ShNum) ? SectionBases[ExitSym->Shndx] : 0);

            ExitFn = (void (*)(void))(BaseE ? (BaseE + ExitSym->Value) : (uint8_t*)ExitSym->Value);
        }
    }

    PInfo("MOD: Calling module_init at %p\n", InitFn);
    InitFn();

    ModuleRecord* Rec = (ModuleRecord*)KMalloc(sizeof(ModuleRecord));
    if (!Rec)
    {
        PError("MOD: Registry alloc failed\n");
        PSuccess("MOD: Installed %s\n", __Path__);
        return 0;
    }

    Rec->Name         = __Path__;
    Rec->SectionBases = SectionBases;
    Rec->ShTbl        = ShTbl;
    Rec->Syms         = Syms;
    Rec->SymBuf       = SymBuf;
    Rec->StrBuf       = StrBuf;
    Rec->SectionCount = ShNum;
    Rec->ZeroStub     = ZeroStub;
    Rec->InitFn       = InitFn;
    Rec->ExitFn       = ExitFn; /* now resolved if present */
    Rec->RefCount     = 1;
    Rec->Next         = 0;

    ModuleRegistryAdd(Rec);

    KFree(Syms);
    KFree(SymBuf);
    KFree(StrBuf);
    KFree(ShTbl);
    KFree(SectionBases);

    PSuccess("MOD: Installed %s\n", __Path__);
    return 0;
}

/**
 * @brief Uninstall an ELF module
 *
 * @details Finds the module record by path, calls module_exit if present, frees all
 * 			allocated section memory and buffers, and removes the record from registry.
 *
 * @param __Path__ Path used to install the module
 * @return 0 on success, -1 on failure
 */
int
UnInstallModule(const char* __Path__)
{
    if (!__Path__)
    {
        PError("MOD: Uninstall invalid path (NULL)\n");
        return -1;
    }

    ModuleRecord* Rec = ModuleRegistryFind(__Path__);
    if (!Rec)
    {
        PError("MOD: Module not found for uninstall: %s\n", __Path__);
        return -1;
    }

    if (Rec->RefCount > 1)
    {
        PError("MOD: Module in use (ref=%d)\n", Rec->RefCount);
        return -1;
    }

    if (Rec->ExitFn)
    {
        PInfo("MOD: Calling module_exit at %p\n", Rec->ExitFn);
        Rec->ExitFn();
    }

    for (long I = 0; I < Rec->SectionCount; I++)
    {
        if (Rec->SectionBases[I] && Rec->SectionBases[I] != (void*)Rec->ZeroStub)
        {
            long Sz = (long)Rec->ShTbl[I].sh_size;
            if (Sz > 0)
            {
                ModFree(Rec->SectionBases[I], (size_t)Sz);
            }
        }
    }

    ModuleRegistryRemove(Rec);

    KFree(Rec->SectionBases);
    KFree(Rec->Syms);
    KFree(Rec->SymBuf);
    KFree(Rec->StrBuf);
    KFree(Rec->ShTbl);
    KFree(Rec);

    PSuccess("MOD: Uninstalled %s\n", __Path__);
    return 0;
}