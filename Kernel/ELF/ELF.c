#include <AllTypes.h>
#include <AxeThreads.h>
#include <IDT.h>
#include <KHeap.h>
#include <KrnPrintf.h>
#include <PMM.h>
#include <Process.h>
#include <PubELF.h>
#include <String.h>
#include <VFS.h>
#include <VMM.h>

/**
 * @brief ELF64 constants and structures (x86_64, little-endian).
 */
#define ElfMag0       0x7F
#define ElfMag1       'E'
#define ElfMag2       'L'
#define ElfMag3       'F'
#define ElfClass64    2
#define ElfData2Lsb   1
#define ElfVersionCur 1

#define ElfTypeExec   2
#define ElfTypeDyn    3
#define ElfMachX86_64 0x3E

#define PhTypeNull   0
#define PhTypeLoad   1
#define PhTypeInterp 3
#define PhTypePhdr   6

/* GNU extensions */
#define PhTypeGnuStack 0x6474E551
#define PhTypeGnuRelro 0x6474E552

#define PfX (1U << 0)
#define PfW (1U << 1)
#define PfR (1U << 2)

typedef struct Elf64Ehdr
{

    unsigned char Ident[16];
    uint16_t      Type;
    uint16_t      Machine;
    uint32_t      Version;
    uint64_t      Entry;
    uint64_t      Phoff;
    uint64_t      Shoff;
    uint32_t      Flags;
    uint16_t      Ehsize;
    uint16_t      Phentsize;
    uint16_t      Phnum;
    uint16_t      Shentsize;
    uint16_t      Shnum;
    uint16_t      Shstrndx;

} Elf64Ehdr;

typedef struct Elf64Phdr
{

    uint32_t Type;
    uint32_t Flags;
    uint64_t Offset;
    uint64_t Vaddr;
    uint64_t Paddr;
    uint64_t Filesz;
    uint64_t Memsz;
    uint64_t Align;

} Elf64Phdr;

/**
 * @brief Convert ELF segment flags to VMM page flags for user mappings.
 *
 * Sets NX only when PF_X is not present. PF_W sets writability; PF_R contributes no
 * special PTE but is tracked for clarity.
 *
 * @param __Pf__ ELF p_flags bitfield.
 *
 * @return VMM page flags suitable for MapPage.
 */
static inline uint64_t
ElfPfToVmmFlags(uint32_t __Pf__)
{
    uint64_t Flags = PTEPRESENT | PTEUSER;
    if (__Pf__ & PfW)
    {
        Flags |= PTEWRITABLE;
    }
    if (!(__Pf__ & PfX))
    {
        Flags |= PTENOEXECUTE;
    }
    return Flags;
}

/**
 * @brief Read exactly a given number of bytes from a file.
 *
 * @param __File__ File handle.
 * @param __Buf__ Destination buffer.
 * @param __Len__ Number of bytes to read.
 *
 * @return 0 on success, -1 on failure or short read.
 */
static int
ReadExact(File* __File__, void* __Buf__, long __Len__)
{
    long ReadCount = VfsRead(__File__, __Buf__, __Len__);
    if (ReadCount != __Len__)
    {
        PError("Elf: ReadExact failed: Want=%ld Got=%ld\n", __Len__, ReadCount);
        return -1;
    }
    return 0;
}

/**
 * @brief Validate ELF64 header for x86_64 ET_EXEC/ET_DYN.
 *
 * @param __Eh__ ELF header pointer.
 *
 * @return 0 if valid, -1 otherwise.
 */
static int
ElfValidateHeader(const Elf64Ehdr* __Eh__)
{
    if (!__Eh__)
    {
        PError("Elf: Header is NULL\n");
        return -1;
    }
    if (__Eh__->Ident[0] != ElfMag0 || __Eh__->Ident[1] != ElfMag1 || __Eh__->Ident[2] != ElfMag2 ||
        __Eh__->Ident[3] != ElfMag3)
    {
        PError("Elf: Bad magic\n");
        return -1;
    }
    if (__Eh__->Ident[4] != ElfClass64)
    {
        PError("Elf: Not ELF64\n");
        return -1;
    }
    if (__Eh__->Ident[5] != ElfData2Lsb)
    {
        PError("Elf: Not LSB\n");
        return -1;
    }
    if (__Eh__->Version != ElfVersionCur)
    {
        PError("Elf: Bad version=%u\n", __Eh__->Version);
        return -1;
    }
    if (!(__Eh__->Type == ElfTypeExec || __Eh__->Type == ElfTypeDyn))
    {
        PError("Elf: Unsupported type=%u (need ET_EXEC or ET_DYN)\n", __Eh__->Type);
        return -1;
    }
    if (__Eh__->Machine != ElfMachX86_64)
    {
        PError("Elf: Not x86_64 machine=%u\n", __Eh__->Machine);
        return -1;
    }
    return 0;
}

/**
 * @brief Compute load base for ET_DYN respecting maximum p_align and page alignment.
 *
 * For ET_EXEC returns 0. For ET_DYN, returns a page-aligned base (UserVirtualBase).
 *
 * @param __Eh__ ELF header pointer.
 * @param __Phdrs__ Program headers buffer.
 * @param __Phnum__ Number of program headers.
 *
 * @return Load base address.
 */
static uint64_t
ElfComputeLoadBase(const Elf64Ehdr* __Eh__, const Elf64Phdr* __Phdrs__, uint16_t __Phnum__)
{
    if (__Eh__->Type == ElfTypeExec)
    {
        return 0;
    }

    uint64_t MaxAlign = PageSize;
    for (uint16_t I = 0; I < __Phnum__; I++)
    {
        if (__Phdrs__[I].Type != PhTypeLoad)
        {
            continue;
        }
        uint64_t A = __Phdrs__[I].Align ? __Phdrs__[I].Align : PageSize;
        if (A > MaxAlign)
        {
            MaxAlign = A;
        }
    }
    /* Choose a fixed base aligned at MaxAlign; later you can randomize for ASLR */
    uint64_t Base = UserVirtualBase;
    Base          = (Base + (MaxAlign - 1)) & ~(MaxAlign - 1);
    return Base;
}

/**
 * @brief Map PT_LOAD page clamping of file and mem subranges, flags.
 *
 * @details For each page covering [PageVa, PageVa+PageSize):
 * 			data_start_in_page = max(0, SegVaddr - PageVa)
 * 			data_end_in_page   = min(PageSize, (SegVaddr + SegFilesz) - PageVa)
 * 			mem_end_in_page    = min(PageSize, (SegVaddr + SegMemsz) - PageVa)
 * 			Then copy file bytes for [data_start_in_page, data_end_in_page) and zero
 * 			the remainder up to mem_end_in_page. Anything beyond mem_end_in_page is untouched.
 *
 *			This handles all p_aligns and misalignments without relying on global deltas.
 *
 * @param __Space__ Main Virtual Space
 * @param __File__ Main ELF File (For Seek)
 * @param __Ph__ PHDS header
 * @param __LoadBase__ Base for load
 *
 * @return 0 if valid, -1 otherwise.
 */
static int
ElfMapLoadSegment(VirtualMemorySpace* __Space__,
                  File*               __File__,
                  const Elf64Phdr*    __Ph__,
                  uint64_t            __LoadBase__)
{
    if (!__Space__ || !__File__ || !__Ph__)
    {
        PError("Elf: MapLoadSegment invalid args\n");
        return -1;
    }
    if (__Ph__->Type != PhTypeLoad)
    {
        return 0;
    }

    const uint64_t SegVaddr  = __LoadBase__ + __Ph__->Vaddr;
    const uint64_t SegOffset = __Ph__->Offset;
    const uint64_t SegFilesz = __Ph__->Filesz;
    const uint64_t SegMemsz  = __Ph__->Memsz;

    const uint64_t SegVaStart = SegVaddr & ~(PageSize - 1);
    const uint64_t SegVaEnd =
        (SegVaddr + SegMemsz + PageSize - 1) & ~(PageSize - 1); /* map up to mem end */
    const uint64_t MapFlags = ((!(__Ph__->Flags & PfX)) ? (PTEPRESENT | PTEUSER | PTENOEXECUTE)
                                                        : (PTEPRESENT | PTEUSER)) |
                              ((__Ph__->Flags & PfW) ? PTEWRITABLE : 0);

    PDebug("Elf: PT_LOAD Vaddr=0x%lx Off=0x%lx Filesz=%lu Memsz=%lu Flags=0x%x\n",
           (unsigned long)SegVaddr,
           (unsigned long)SegOffset,
           (unsigned long)SegFilesz,
           (unsigned long)SegMemsz,
           (unsigned)__Ph__->Flags);

    for (uint64_t PageVa = SegVaStart; PageVa < SegVaEnd; PageVa += PageSize)
    {
        uint64_t Phys = AllocPage();
        if (!Phys)
        {
            PError("Elf: AllocPage failed\n");
            return -1;
        }

        if (MapPage(__Space__, PageVa, Phys, MapFlags) != 1)
        {
            PError("Elf: MapPage failed Va=0x%lx Phys=0x%lx Flags=0x%lx\n",
                   (unsigned long)PageVa,
                   (unsigned long)Phys,
                   (unsigned long)MapFlags);
            FreePage(Phys);
            return -1;
        }

        /* Compute clamped regions in this page */
        uint64_t data_start_in_page = 0;
        uint64_t data_end_in_page   = 0;
        uint64_t mem_end_in_page    = 0;

        /* Where segment starts inside this page */
        if (SegVaddr > PageVa)
        {
            data_start_in_page = SegVaddr - PageVa;
        }

        /* Where file-backed bytes end inside this page */
        {
            uint64_t file_end_va = SegVaddr + SegFilesz;
            if (file_end_va > PageVa)
            {
                data_end_in_page =
                    (file_end_va - PageVa > PageSize) ? PageSize : (file_end_va - PageVa);
            }
        }

        /* Where mem (incl. BSS) ends inside this page */
        {
            uint64_t mem_end_va = SegVaddr + SegMemsz;
            if (mem_end_va > PageVa)
            {
                mem_end_in_page =
                    (mem_end_va - PageVa > PageSize) ? PageSize : (mem_end_va - PageVa);
            }
        }

        /* Prepare bounce page: zero up to mem_end_in_page */
        char* Bounce = (char*)KMalloc(PageSize);
        if (!Bounce)
        {
            PError("Elf: KMalloc bounce failed\n");
            return -1;
        }
        for (uint64_t i = 0; i < mem_end_in_page; i++)
        {
            Bounce[i] = 0;
        }

        /* Copy file-backed bytes if present */
        if (data_end_in_page > data_start_in_page)
        {
            uint64_t copy_len = data_end_in_page - data_start_in_page;
            uint64_t file_pos = SegOffset + (PageVa + data_start_in_page - SegVaddr);

            if (VfsLseek(__File__, (long)file_pos, VSeekSET) < 0)
            {
                PError("Elf: Lseek failed FilePos=0x%lx\n", (unsigned long)file_pos);
                KFree(Bounce);
                return -1;
            }
            long rd = VfsRead(__File__, Bounce + data_start_in_page, (long)copy_len);
            if (rd != (long)copy_len)
            {
                PError("Elf: Read short: want=%lu got=%ld FilePos=0x%lx\n",
                       (unsigned long)copy_len,
                       rd,
                       (unsigned long)file_pos);
                KFree(Bounce);
                return -1;
            }
        }

        /* Write bounce into mapped physical page */
        volatile char* KPage = (volatile char*)PhysToVirt(Phys);
        for (uint64_t i = 0; i < mem_end_in_page; i++)
        {
            KPage[i] = Bounce[i];
        }
        /* If mem_end_in_page < PageSize, leave the remainder untouched (outside segment) */

        KFree(Bounce);
    }
    return 0;
}

/**
 * @brief Map all PT_LOAD segments from an array of program headers (ET_EXEC/ET_DYN aware).
 *
 * @param __Space__ Target address space.
 * @param __File__ ELF file handle.
 * @param __Phdrs__ Program headers buffer.
 * @param __Phnum__ Number of program headers.
 * @param __LoadBase__ Load base to add to p_vaddr.
 *
 * @return 0 on success, -1 on failure.
 */
int
ElfMapLoadSegments(VirtualMemorySpace* __Space__,
                   File*               __File__,
                   void*               __Phdrs__,
                   uint16_t            __Phnum__,
                   uint64_t            __LoadBase__)
{
    if (!__Space__ || !__File__ || !__Phdrs__)
    {
        PError("Elf: MapLoadSegments invalid args\n");
        return -1;
    }
    Elf64Phdr* Ph = (Elf64Phdr*)__Phdrs__;

    for (uint16_t I = 0; I < __Phnum__; I++)
    {
        int Res = ElfMapLoadSegment(__Space__, __File__, &Ph[I], __LoadBase__);
        if (Res != 0)
        {
            PError("Elf: MapLoadSegment failed Index=%u\n", (unsigned)I);
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Build initial user stack with argc/argv/envp and packed strings, handling zero-argc/envp
 * safely.
 *
 * @details Layout at final RSP (lowest address first):
 *   	[argc]
 *   	[argv pointers ...] [NULL]
 *   	[envp pointers ...] [NULL]
 * 		Above that, at higher addresses within mapped stack pages, the strings are packed top-down.
 *
 * @details The pointer block is carved directly below the final strings cursor, ensuring it lies
 * 			within mapped pages (between LowerVa and UpperVa). A single guard page below the stack
 * 			remains unmapped.
 *
 * @param __Space__ Target address space.
 * @param __Argv__ NULL-terminated argv vector (may be NULL).
 * @param __Envp__ NULL-terminated envp vector (may be NULL).
 * @param __StackExecutable__ 1 to allow executable stack, 0 to mark NX.
 *
 * @return New user-mode RSP or 0 on failure.
 */
uint64_t
ElfSetupUserStack(VirtualMemorySpace* __Space__,
                  const char* const*  __Argv__,
                  const char* const*  __Envp__,
                  int                 __StackExecutable__)
{
    if (!__Space__)
    {
        PError("Elf: SetupUserStack Space=NULL\n");
        return 0;
    }

    uint64_t StackTop   = UserVirtualBase + 0x0000000001000000ULL;
    uint64_t MapPages   = (KStackSize / PageSize);
    uint64_t LowerVa    = StackTop - (MapPages * PageSize);
    uint64_t UpperVa    = StackTop;
    uint64_t StrsCursor = UpperVa;

    PDebug("Elf: Stack map LowerVa=0x%lx UpperVa=0x%lx Pages=%lu Exec=%d\n",
           (unsigned long)LowerVa,
           (unsigned long)UpperVa,
           (unsigned long)MapPages,
           __StackExecutable__);

    /* Map stack pages (leave one guard page below unmapped by design since LowerVa is lowest
     * mapped) */
    for (uint64_t I = 0; I < MapPages; I++)
    {
        uint64_t Phys = AllocPage();
        if (!Phys)
        {
            PError("Elf: Stack AllocPage failed\n");
            return 0;
        }
        uint64_t Va    = UpperVa - ((I + 1) * PageSize);
        uint64_t Flags = PTEPRESENT | PTEUSER | PTEWRITABLE;
        if (!__StackExecutable__)
        {
            Flags |= PTENOEXECUTE;
        }
        if (MapPage(__Space__, Va, Phys, Flags) != 1)
        {
            PError("Elf: Stack MapPage failed Va=0x%lx\n", (unsigned long)Va);
            return 0;
        }
    }

    long Argc = 0;
    long Envc = 0;
    if (__Argv__)
    {
        while (__Argv__[Argc])
        {
            Argc++;
        }
    }
    if (__Envp__)
    {
        while (__Envp__[Envc])
        {
            Envc++;
        }
    }

    PDebug("Elf: argc=%ld envc=%ld\n", Argc, Envc);

    /* Temporary arrays for pointer values (kernel-side) */
    uint64_t* ArgvPtrsTmp = NULL;
    uint64_t* EnvpPtrsTmp = NULL;

    if (Argc > 0)
    {
        ArgvPtrsTmp = (uint64_t*)KMalloc((size_t)Argc * sizeof(uint64_t));
        if (!ArgvPtrsTmp)
        {
            PError("Elf: KMalloc ArgvPtrsTmp failed\n");
            return 0;
        }
    }
    if (Envc > 0)
    {
        EnvpPtrsTmp = (uint64_t*)KMalloc((size_t)Envc * sizeof(uint64_t));
        if (!EnvpPtrsTmp)
        {
            if (ArgvPtrsTmp)
            {
                KFree(ArgvPtrsTmp);
            }
            PError("Elf: KMalloc EnvpPtrsTmp failed\n");
            return 0;
        }
    }

    /* Pack argv strings, record virtual addresses */
    for (long I = 0; I < Argc; I++)
    {
        const char* S    = __Argv__[I];
        long        Slen = (long)strlen(S);
        StrsCursor -= (uint64_t)(Slen + 1);

        if (StrsCursor < LowerVa)
        {
            PError("Elf: argv packing underrun LowerVa=0x%lx StrsCursor=0x%lx\n",
                   (unsigned long)LowerVa,
                   (unsigned long)StrsCursor);
            if (ArgvPtrsTmp)
            {
                KFree(ArgvPtrsTmp);
            }
            if (EnvpPtrsTmp)
            {
                KFree(EnvpPtrsTmp);
            }
            return 0;
        }

        uint64_t Phys = GetPhysicalAddress(__Space__, StrsCursor);
        if (!Phys)
        {
            PError("Elf: Arg string phys=0 for Va=0x%lx\n", (unsigned long)StrsCursor);
            if (ArgvPtrsTmp)
            {
                KFree(ArgvPtrsTmp);
            }
            if (EnvpPtrsTmp)
            {
                KFree(EnvpPtrsTmp);
            }
            return 0;
        }
        volatile char* KDst = (volatile char*)PhysToVirt(Phys);
        for (long J = 0; J < Slen; J++)
        {
            KDst[J] = S[J];
        }
        KDst[Slen] = '\0';

        ArgvPtrsTmp[I] = StrsCursor;
    }

    /* Pack envp strings, record virtual addresses */
    for (long I = 0; I < Envc; I++)
    {
        const char* S    = __Envp__[I];
        long        Slen = (long)strlen(S);
        StrsCursor -= (uint64_t)(Slen + 1);

        if (StrsCursor < LowerVa)
        {
            PError("Elf: envp packing underrun LowerVa=0x%lx StrsCursor=0x%lx\n",
                   (unsigned long)LowerVa,
                   (unsigned long)StrsCursor);
            if (ArgvPtrsTmp)
            {
                KFree(ArgvPtrsTmp);
            }
            if (EnvpPtrsTmp)
            {
                KFree(EnvpPtrsTmp);
            }
            return 0;
        }

        uint64_t Phys = GetPhysicalAddress(__Space__, StrsCursor);
        if (!Phys)
        {
            PError("Elf: Env string phys=0 for Va=0x%lx\n", (unsigned long)StrsCursor);
            if (ArgvPtrsTmp)
            {
                KFree(ArgvPtrsTmp);
            }
            if (EnvpPtrsTmp)
            {
                KFree(EnvpPtrsTmp);
            }
            return 0;
        }
        volatile char* KDst = (volatile char*)PhysToVirt(Phys);
        for (long J = 0; J < Slen; J++)
        {
            KDst[J] = S[J];
        }
        KDst[Slen] = '\0';

        EnvpPtrsTmp[I] = StrsCursor;
    }

    /* Carve pointer block just below StrsCursor, aligned, ensure it lies within mapped stack */
    uint64_t PtrBlockCount = 1 /* argc */ + (uint64_t)(Argc + 1) + (uint64_t)(Envc + 1);
    uint64_t PtrBlockSize  = PtrBlockCount * sizeof(uint64_t);
    uint64_t PtrBase       = (StrsCursor - PtrBlockSize) & ~0xFULL;

    if (PtrBase < LowerVa)
    {
        PError("Elf: Pointer block would underrun LowerVa: PtrBase=0x%lx LowerVa=0x%lx\n",
               (unsigned long)PtrBase,
               (unsigned long)LowerVa);
        if (ArgvPtrsTmp)
        {
            KFree(ArgvPtrsTmp);
        }
        if (EnvpPtrsTmp)
        {
            KFree(EnvpPtrsTmp);
        }
        return 0;
    }

    /* Final RSP points to argc at PtrBase */
    uint64_t ArgcVa     = PtrBase;
    uint64_t ArgvPtrsVa = ArgcVa + sizeof(uint64_t);
    uint64_t EnvpPtrsVa = ArgvPtrsVa + ((uint64_t)Argc + 1) * sizeof(uint64_t);

    /* Validate the entire pointer block is mapped */
    for (uint64_t Off = 0; Off < PtrBlockSize; Off += PageSize)
    {
        uint64_t Phys = GetPhysicalAddress(__Space__, ArgcVa + Off);
        if (!Phys)
        {
            PError("Elf: Pointer block outside mapped stack ArgcVa=0x%lx\n", (unsigned long)ArgcVa);
            if (ArgvPtrsTmp)
            {
                KFree(ArgvPtrsTmp);
            }
            if (EnvpPtrsTmp)
            {
                KFree(EnvpPtrsTmp);
            }
            return 0;
        }
    }

    /* Write argc */
    {
        uint64_t           Phys  = GetPhysicalAddress(__Space__, ArgcVa);
        volatile uint64_t* KArgc = (volatile uint64_t*)PhysToVirt(Phys);
        *KArgc                   = (uint64_t)Argc;
    }

    /* Write argv pointers and NULL */
    for (long I = 0; I < Argc; I++)
    {
        uint64_t           PtrVa = ArgvPtrsVa + (uint64_t)I * sizeof(uint64_t);
        uint64_t           Phys  = GetPhysicalAddress(__Space__, PtrVa);
        volatile uint64_t* KPtr  = (volatile uint64_t*)PhysToVirt(Phys);
        *KPtr                    = ArgvPtrsTmp[I];
    }
    {
        uint64_t           NullVa = ArgvPtrsVa + (uint64_t)Argc * sizeof(uint64_t);
        uint64_t           Phys   = GetPhysicalAddress(__Space__, NullVa);
        volatile uint64_t* KPtr   = (volatile uint64_t*)PhysToVirt(Phys);
        *KPtr                     = 0;
    }

    /* Write envp pointers and NULL */
    for (long I = 0; I < Envc; I++)
    {
        uint64_t           PtrVa = EnvpPtrsVa + (uint64_t)I * sizeof(uint64_t);
        uint64_t           Phys  = GetPhysicalAddress(__Space__, PtrVa);
        volatile uint64_t* KPtr  = (volatile uint64_t*)PhysToVirt(Phys);
        *KPtr                    = EnvpPtrsTmp[I];
    }
    {
        uint64_t           NullVa = EnvpPtrsVa + (uint64_t)Envc * sizeof(uint64_t);
        uint64_t           Phys   = GetPhysicalAddress(__Space__, NullVa);
        volatile uint64_t* KPtr   = (volatile uint64_t*)PhysToVirt(Phys);
        *KPtr                     = 0;
    }

    if (ArgvPtrsTmp)
    {
        KFree(ArgvPtrsTmp);
    }
    if (EnvpPtrsTmp)
    {
        KFree(EnvpPtrsTmp);
    }

    PDebug("Elf: Stack finalized Rsp=0x%lx PtrBase=0x%lx StrsCursor=0x%lx\n",
           (unsigned long)ArgcVa,
           (unsigned long)PtrBase,
           (unsigned long)StrsCursor);
    return ArgcVa;
}

/**
 * @brief Load ELF64 (ET_EXEC or ET_DYN): validate header, map segments, handle PT_GNU_STACK, build
 * stack, produce image.
 *
 * @param __Proc__ Process pointer.
 * @param __Path__ Path to ELF binary.
 * @param __Argv__ argv vector.
 * @param __Envp__ envp vector.
 * @param __OutImage__ Output image (entry, stack, space).
 *
 * @return 0 on success, -1 on failure.
 */
int
ElfLoadExec(Process*           __Proc__,
            const char*        __Path__,
            const char* const* __Argv__,
            const char* const* __Envp__,
            ElfExecImage*      __OutImage__)
{
    PDebug("ElfLoadExec[enter]: Pid=%ld Path=%s\n", __Proc__ ? __Proc__->PID : -1, __Path__);
    if (!__Proc__ || !__Path__ || !__OutImage__)
    {
        PError("ElfLoadExec: Invalid arguments\n");
        return -1;
    }

    File* FileHandle = VfsOpen(__Path__, VFlgRDONLY);
    if (!FileHandle)
    {
        PError("ElfLoadExec: VfsOpen failed Path=%s\n", __Path__);
        return -1;
    }

    Elf64Ehdr Eh = {0};
    if (ReadExact(FileHandle, &Eh, (long)sizeof(Eh)) != 0)
    {
        VfsClose(FileHandle);
        return -1;
    }
    if (ElfValidateHeader(&Eh) != 0)
    {
        VfsClose(FileHandle);
        return -1;
    }

    PDebug("ElfLoadExec[hdr]: Type=%u Entry=0x%lx Phoff=0x%lx Phnum=%u Phentsize=%u\n",
           (unsigned)Eh.Type,
           (unsigned long)Eh.Entry,
           (unsigned long)Eh.Phoff,
           (unsigned)Eh.Phnum,
           (unsigned)Eh.Phentsize);

    if (Eh.Phentsize != sizeof(Elf64Phdr))
    {
        PError("ElfLoadExec: Bad Phentsize\n");
        VfsClose(FileHandle);
        return -1;
    }
    if (VfsLseek(FileHandle, (long)Eh.Phoff, VSeekSET) < 0)
    {
        PError("ElfLoadExec: Lseek Phdrs failed\n");
        VfsClose(FileHandle);
        return -1;
    }

    Elf64Phdr* Phdrs = (Elf64Phdr*)KMalloc((size_t)Eh.Phnum * sizeof(Elf64Phdr));
    if (!Phdrs)
    {
        PError("ElfLoadExec: KMalloc Phdrs failed\n");
        VfsClose(FileHandle);
        return -1;
    }
    if (ReadExact(FileHandle, Phdrs, (long)((long)Eh.Phnum * (long)sizeof(Elf64Phdr))) != 0)
    {
        PError("ElfLoadExec: Read Phdrs failed\n");
        KFree(Phdrs);
        VfsClose(FileHandle);
        return -1;
    }

    /* Parse PT_GNU_STACK (if present) */
    int StackExecutable = 0;
    for (uint16_t I = 0; I < Eh.Phnum; I++)
    {
        if (Phdrs[I].Type == PhTypeGnuStack)
        {
            StackExecutable = ((Phdrs[I].Flags & PfX) != 0);
            PDebug("Elf: PT_GNU_STACK Flags=0x%x Exec=%d\n",
                   (unsigned)Phdrs[I].Flags,
                   StackExecutable);
        }
        else if (Phdrs[I].Type == PhTypeInterp)
        {
            PError("Elf: PT_INTERP present, dynamic interpreter not supported yet\n");
            KFree(Phdrs);
            VfsClose(FileHandle);
            return -1;
        }
    }

    VirtualMemorySpace* Space = CreateVirtualSpace();
    if (!Space)
    {
        PError("ElfLoadExec: CreateVirtualSpace failed\n");
        KFree(Phdrs);
        VfsClose(FileHandle);
        return -1;
    }

    uint64_t LoadBase = ElfComputeLoadBase(&Eh, Phdrs, Eh.Phnum);

    if (ElfMapLoadSegments(Space, FileHandle, Phdrs, Eh.Phnum, LoadBase) != 0)
    {
        PError("ElfLoadExec: Segment mapping failed\n");
        DestroyVirtualSpace(Space);
        KFree(Phdrs);
        VfsClose(FileHandle);
        return -1;
    }

    uint64_t Rsp = ElfSetupUserStack(Space, __Argv__, __Envp__, StackExecutable);
    if (!Rsp)
    {
        PError("ElfLoadExec: SetupUserStack failed\n");
        DestroyVirtualSpace(Space);
        KFree(Phdrs);
        VfsClose(FileHandle);
        return -1;
    }

    __OutImage__->Entry           = LoadBase + Eh.Entry;
    __OutImage__->UserSp          = Rsp;
    __OutImage__->Space           = Space;
    __OutImage__->LoadBase        = LoadBase;
    __OutImage__->StackExecutable = StackExecutable;

    PDebug("ElfLoadExec[ok]: Entry=0x%lx LoadBase=0x%lx Rsp=0x%lx Pml4=0x%lx\n",
           (unsigned long)__OutImage__->Entry,
           (unsigned long)__OutImage__->LoadBase,
           (unsigned long)__OutImage__->UserSp,
           (unsigned long)__OutImage__->Space->PhysicalBase);

    KFree(Phdrs);
    VfsClose(FileHandle);
    return 0;
}

/**
 * @brief Replace current process image with the loaded ELF program, set thread context, switch
 * space, enqueue.
 *
 * @param __Proc__ Process pointer.
 * @param __Path__ Path to ELF binary.
 * @param __Argv__ Argument vector (NULL-terminated).
 * @param __Envp__ Environment vector (NULL-terminated).
 *
 * @return 0 on success, -1 on failure.
 */
int
ProcExecve(Process*           __Proc__,
           const char*        __Path__,
           const char* const* __Argv__,
           const char* const* __Envp__)
{
    PDebug("ProcExec[enter]: Pid=%ld Path=%s\n", __Proc__ ? __Proc__->PID : -1, __Path__);
    if (!__Proc__ || !__Path__)
    {
        PError("ProcExec: Invalid args\n");
        return -1;
    }
    if (!__Proc__->MainThread)
    {
        PError("ProcExec: MainThread missing pid=%ld\n", __Proc__->PID);
        return -1;
    }

    ElfExecImage Image = {0};
    if (ElfLoadExec(__Proc__, __Path__, __Argv__, __Envp__, &Image) != 0)
    {
        PError("ProcExec: ElfLoadExec failed Path=%s\n", __Path__);
        return -1;
    }

    Thread* Main         = __Proc__->MainThread;
    Main->Type           = ThreadTypeUser;
    Main->Context.Rip    = Image.Entry;
    Main->Context.Rsp    = Image.UserSp;
    Main->PageDirectory  = (uint64_t)Image.Space->PhysicalBase;
    Main->UserStack      = Image.UserSp;
    Main->StackSize      = KStackSize;
    Main->MemoryUsage    = 0; /*N/A*/
    Main->Context.Rflags = 0x202;

    Main->Context.Cs = UserCodeSelector;
    Main->Context.Ss = UserDataSelector;
    Main->Context.Ds = UserDataSelector;
    Main->Context.Es = UserDataSelector;
    Main->Context.Fs = UserDataSelector;
    Main->Context.Gs = UserDataSelector;

    PDebug("ProcExec[pre-switch]: Rip=0x%lx Rsp=0x%lx Cs=0x%x Ss=0x%x Pml4=0x%lx\n",
           (unsigned long)Main->Context.Rip,
           (unsigned long)Main->Context.Rsp,
           (unsigned int)Main->Context.Cs,
           (unsigned int)Main->Context.Ss,
           (unsigned long)Image.Space->PhysicalBase);

    PDebug("ProcExec[post-switch]: Pid=%ld Entry=0x%lx Rsp=0x%lx Pml4=0x%lx\n",
           __Proc__->PID,
           (unsigned long)Image.Entry,
           (unsigned long)Image.UserSp,
           (unsigned long)Image.Space->PhysicalBase);

    Main->State = ThreadStateReady;
    ThreadExecute(Main);
    PDebug("ProcExec[enqueue]: ThreadId=%u State=%d\n", Main->ThreadId, (int)Main->State);

    return 0;
}