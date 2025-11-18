#include <KHeap.h>
#include <RamFs.h>

/**
 * @brief Global RamFS context
 * Maintains the root of the filesystem and magic number for validation
 */
RamFSContext RamFS = {
    .Root  = 0,         /**< Root directory node */
    .Magic = RamFSMagic /**< Magic number for context validation */
};

/**
 * @brief Attach a path into the RamFS hierarchy, auto-creating intermediate directories
 *
 * Builds or finds each directory segment in the given absolute path, and creates
 * the leaf node as a file or directory. For files, data and size are assigned.
 *
 * @param __Root__ Root directory node of the filesystem
 * @param __FullPath__ Absolute path to create (e.g., "/etc/init")
 * @param __Type__ Node type: RamFSNode_File or RamFSNode_Directory
 * @param __Data__ Pointer to file data (files only, may be NULL for directories)
 * @param __Size__ Size of file data in bytes (files only)
 * @return Pointer to the leaf node, or NULL on failure
 *
 * @note This function modifies the filesystem hierarchy by creating missing directories
 * @note Memory allocation failures during path creation will result in partial paths
 */
RamFSNode*
RamFSAttachPath(RamFSNode*     __Root__,
                const char*    __FullPath__,
                RamFSNodeType  __Type__,
                const uint8_t* __Data__,
                uint32_t       __Size__)
{
    if (!__Root__ || !__FullPath__)
    {
        return 0;
    }

    if (__FullPath__[0] != '/')
    {
        return 0;
    }

    const char* PathSegment = __FullPath__;
    PathSegment++; /*Skip leading slash*/

    RamFSNode*  Cur      = __Root__;
    const char* SegStart = PathSegment;

    /*Create/find intermediate directories*/
    while (*PathSegment)
    {
        if (*PathSegment == '/')
        {
            uint32_t SegLen = (uint32_t)(PathSegment - SegStart);
            if (SegLen == 0)
            {
                PathSegment++;
                SegStart = PathSegment;
                continue;
            }

            /*Find existing directory child*/
            RamFSNode* Next = 0;
            for (uint32_t I = 0; I < Cur->ChildCount; I++)
            {
                RamFSNode* C = Cur->Children[I];
                if (!C || C->Type != RamFSNode_Directory)
                {
                    continue;
                }

                uint32_t K = 0;
                for (; K < SegLen && C->Name[K] && C->Name[K] == SegStart[K]; K++)
                    ;
                if (K == SegLen && C->Name[K] == '\0')
                {
                    Next = C;
                    break;
                }
            }

            /*Create if missing*/
            if (!Next)
            {
                char* Name = (char*)KMalloc(SegLen + 1);
                if (!Name)
                {
                    return 0;
                }

                for (uint32_t I = 0; I < SegLen; I++)
                {
                    Name[I] = SegStart[I];
                }
                Name[SegLen] = '\0';

                Next = RamFSCreateNode(Name, RamFSNode_Directory);
                if (!Next)
                {
                    return 0;
                }

                RamFSAddChild(Cur, Next);
            }

            Cur = Next;
            PathSegment++;
            SegStart = PathSegment;
            continue;
        }
        PathSegment++;
    }

    /*Leaf segment name*/
    uint32_t LeafLen = (uint32_t)(PathSegment - SegStart);
    if (LeafLen == 0)
    {
        return Cur;
    }

    /*Find existing leaf*/
    RamFSNode* Leaf = 0;
    for (uint32_t I = 0; I < Cur->ChildCount; I++)
    {
        RamFSNode* C = Cur->Children[I];
        if (!C)
        {
            continue;
        }

        uint32_t K = 0;
        for (; K < LeafLen && C->Name[K] && C->Name[K] == SegStart[K]; K++)
            ;
        if (K == LeafLen && C->Name[K] == '\0')
        {
            Leaf = C;
            break;
        }
    }

    /*Create leaf if missing*/
    if (!Leaf)
    {
        char* Name = (char*)KMalloc(LeafLen + 1);
        if (!Name)
        {
            return 0;
        }

        for (uint32_t I = 0; I < LeafLen; I++)
        {
            Name[I] = SegStart[I];
        }
        Name[LeafLen] = '\0';

        Leaf = RamFSCreateNode(Name, __Type__);
        if (!Leaf)
        {
            return 0;
        }

        RamFSAddChild(Cur, Leaf);
    }

    if (__Type__ == RamFSNode_File)
    {
        Leaf->Data = __Data__;
        Leaf->Size = __Size__;
    }

    return Leaf;
}

/**
 * @brief Parse and mount a CPIO "newc" initramfs image into RamFS
 *
 * Sequentially walks the CPIO archive entries, validating headers and creating
 * the corresponding filesystem nodes. Each entry contains a 110-byte header
 * followed by the filename, padding, file data, and more padding.
 *
 * Processing steps for each entry:
 * 		Validate header magic ("070701")
 * 		Parse NameSize and FileSize from ASCII hex fields
 * 		Extract filename and align to 4-byte boundary
 * 		For files: extract data and align to 4-byte boundary
 * 		Create appropriate RamFS node (file/directory) and attach to hierarchy
 * 		Stop processing at "TRAILER!!!" terminator entry
 *
 * @param __Image__ Pointer to CPIO newc archive in memory
 * @param __Length__ Size of image in bytes
 * @return Root node of mounted filesystem, or NULL on failure
 *
 * @note Memory layout per entry: [110-byte header | name (NUL-included) | padding | data | padding]
 * @note All numeric fields in CPIO header are 8-character ASCII hex values
 * @todo Implement UnMount/UMount function for cleanup
 */

RamFSNode*
RamFSMount(const void* __Image__, size_t __Length__)
{
    RamFSNode* Root = RamFSEnsureRoot();
    if (!Root || !__Image__ || __Length__ == 0)
    {
        return 0;
    }

    const uint8_t* Buf = (const uint8_t*)__Image__;
    uint32_t       Off = 0;

    while (Off + 6 <= (uint32_t)__Length__)
    {

        /*Align to 4-byte boundary for header*/
        Off = CpioAlignUp(Off, CpioAlign);
        if (Off + 110 > (uint32_t)__Length__)
        {
            break;
        }

        /*Check magic "070701"*/
        const char* Magic = (const char*)(Buf + Off);
        if (!(Magic[0] == '0' && Magic[1] == '7' && Magic[2] == '0' && Magic[3] == '7' &&
              Magic[4] == '0' && Magic[5] == '1'))
        {
            break; /*Invalid magic*/
        }

        /*Header field pointers*/
        const char* Mode     = (const char*)(Buf + Off + 14);
        const char* FileSize = (const char*)(Buf + Off + 54);
        const char* NameSize = (const char*)(Buf + Off + 94);

        Off += 110; /*Past header*/

        uint32_t NameLen  = CpioParseHex(NameSize); /*Includes NUL*/
        uint32_t DataLen  = CpioParseHex(FileSize);
        uint32_t ModeBits = CpioParseHex(Mode);

        /*Name string*/
        uint32_t NameOff = Off;
        uint32_t NameEnd = NameOff + NameLen;
        if (NameEnd > (uint32_t)__Length__)
        {
            break;
        }

        const char* NamePtr = (const char*)(Buf + NameOff);

        /*Trailer terminator*/
        if (NameLen >= 11)
        {
            uint32_t Match = 1;
            for (uint32_t I = 0; I < 11; I++)
            {
                if (NamePtr[I] != CpioTrailer[I])
                {
                    Match = 0;
                    break;
                }
            }
            if (Match)
            {
                break;
            }
        }

        /*Advance to data (aligned after name)*/
        Off = CpioAlignUp(NameEnd, CpioAlign);

        /*Determine node type: dir (040000) or file (0100000)*/
        RamFSNodeType NType = RamFSNode_File;
        if ((ModeBits & 0xF000) == 0x4000)
        {
            NType = RamFSNode_Directory;
        }
        else if ((ModeBits & 0xF000) == 0x8000)
        {
            NType = RamFSNode_File;
        }

        /*Data pointer for files*/
        const uint8_t* DataPtr = 0;
        if (NType == RamFSNode_File)
        {
            uint32_t DataOff = Off;
            uint32_t DataEnd = DataOff + DataLen;
            if (DataEnd > (uint32_t)__Length__)
            {
                break;
            }

            DataPtr = Buf + DataOff;

            /*Advance past data to next header (aligned)*/
            Off = CpioAlignUp(DataEnd, CpioAlign);
        }

        /*Build absolute path: cpio names are relative (no leading slash)*/
        uint32_t RawLen = 0;
        while (NamePtr[RawLen] != '\0')
        {
            RawLen++;
        }

        char* FullPath = (char*)KMalloc(RawLen + 2);
        if (!FullPath)
        {
            return 0;
        }

        FullPath[0] = '/';
        for (uint32_t I = 0; I < RawLen; I++)
        {
            FullPath[I + 1] = NamePtr[I];
        }
        FullPath[RawLen + 1] = '\0';

        /*Attach into hierarchy*/
        (void)RamFSAttachPath(Root, FullPath, NType, DataPtr, DataLen);
    }

    return RamFS.Root;
}

/**
 * @brief Resolve a RamFS node by absolute path
 *
 * Traverses the RamFS directory hierarchy to find the node corresponding
 * to the given absolute path. Splits the path into components and walks
 * through the directory structure.
 *
 * @param __Root__ Root node of the mounted RamFS filesystem
 * @param __Path__ Absolute path to resolve (must start with '/')
 * @return Pointer to the node if found, otherwise NULL
 *
 * @note Path must be absolute (start with '/')
 * @note Returns NULL if path is invalid or node doesn't exist
 */
RamFSNode*
RamFSLookup(RamFSNode* __Root__, const char* __Path__)
{
    if (!__Root__ || !__Path__ || __Path__[0] != '/')
    {
        return 0;
    }

    const char* PathSegment = __Path__;
    PathSegment++; /*Skip leading*/

    RamFSNode*  Cur      = __Root__;
    const char* SegStart = PathSegment;

    while (*PathSegment)
    {
        if (*PathSegment == '/')
        {
            uint32_t SegLen = (uint32_t)(PathSegment - SegStart);
            if (SegLen == 0)
            {
                PathSegment++;
                SegStart = PathSegment;
                continue;
            }

            RamFSNode* Next = 0;
            for (uint32_t I = 0; I < Cur->ChildCount; I++)
            {
                RamFSNode* C = Cur->Children[I];
                if (!C)
                {
                    continue;
                }

                uint32_t K = 0;
                for (; K < SegLen && C->Name[K] && C->Name[K] == SegStart[K]; K++)
                    ;
                if (K == SegLen && C->Name[K] == '\0')
                {
                    Next = C;
                    break;
                }
            }

            if (!Next)
            {
                return 0;
            }

            Cur = Next;
            PathSegment++;
            SegStart = PathSegment;
            continue;
        }
        PathSegment++;
    }

    /*Leaf segment*/
    uint32_t LeafLen = (uint32_t)(PathSegment - SegStart);
    if (LeafLen == 0)
    {
        return Cur;
    }

    for (uint32_t I = 0; I < Cur->ChildCount; I++)
    {
        RamFSNode* C = Cur->Children[I];
        if (!C)
        {
            continue;
        }

        uint32_t K = 0;
        for (; K < LeafLen && C->Name[K] && C->Name[K] == SegStart[K]; K++)
            ;
        if (K == LeafLen && C->Name[K] == '\0')
        {
            return C;
        }
    }

    return 0;
}