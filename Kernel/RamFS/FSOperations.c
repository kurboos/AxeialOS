#include <KHeap.h>
#include <RamFs.h>

/**
 * @brief Read bytes from a file node
 *
 * Reads data from a RamFS file node into a user buffer, starting at the
 * specified offset. Handles bounds checking and partial reads. This function
 * is similar to standard POSIX read() but operates on RamFS-specific node
 * structures rather than file descriptors.
 *
 * The implementation performs bounds checking to prevent buffer overflows
 * and ensures that reads don't exceed the file size. For partial reads at
 * EOF, it returns the available bytes rather than an error.
 *
 * @param __Node__ File node to read from (must be RamFSNode_File type)
 * @param __Offset__ Byte offset within the file to start reading (0-based)
 * @param __Buffer__ Destination buffer for read data (must be valid)
 * @param __Length__ Maximum number of bytes to read
 * @return Number of bytes actually read (0 on error, EOF, or invalid node)
 *
 * @note Only works on file nodes; directories return 0
 * @note Similar to Kernel/PMM/PMM.c's bounds checking in memory operations
 * @note No alignment requirements for buffer or offset
 *
 * @see RamFSNode structure in RamFs.h for node format
 * @see RamFSLookup() for path-to-node resolution
 */
size_t
RamFSRead(RamFSNode* __Node__, size_t __Offset__, void* __Buffer__, size_t __Length__)
{
    /* Validate input parameters - similar to PMM validation patterns */
    if (!__Node__ || !__Buffer__)
    {
        return 0;
    }

    /* Only file nodes contain readable data */
    if (__Node__->Type != RamFSNode_File)
    {
        return 0;
    }

    /* Check if offset is beyond file size (EOF) */
    if (__Offset__ >= __Node__->Size)
    {
        return 0;
    }

    /* Calculate available bytes from offset to EOF */
    size_t Available = (size_t)(__Node__->Size) - __Offset__;

    /* Limit read to available bytes (partial read at EOF) */
    if (__Length__ > Available)
    {
        __Length__ = Available;
    }

    /* Perform byte-by-byte copy (no optimization needed for RAM) */
    const uint8_t* Src = __Node__->Data + __Offset__;
    uint8_t*       Dst = (uint8_t*)__Buffer__;

    for (size_t I = 0; I < __Length__; I++)
    {
        Dst[I] = Src[I];
    }

    return __Length__;
}

/**
 * @brief Check if a path exists in the mounted RamFS image
 *
 * Performs a path lookup to determine if a given absolute path exists within
 * the currently mounted RamFS filesystem. This is a lightweight existence
 * check that doesn't retrieve file metadata.
 *
 * @param __Path__ Absolute path string to check (must start with '/')
 * @return 1 if the path exists and is accessible, 0 otherwise
 *
 * @note Requires RamFS.Root to be initialized (filesystem mounted)
 * @note Similar to POSIX access() but only checks existence, not permissions
 * @note Used internally by RamFSIsDir() and RamFSIsFile() for validation
 *
 * @see RamFSLookup() for the underlying path resolution mechanism
 * @see RamFSIsDir() and RamFSIsFile() for type-specific existence checks
 */
int
RamFSExists(const char* __Path__)
{
    /* Validate inputs and filesystem state */
    if (!__Path__ || !RamFS.Root)
    {
        return 0; /* Invalid input or filesystem not mounted */
    }

    /* Perform path lookup - returns NULL if path doesn't exist */
    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    return (Node != 0) ? 1 : 0;
}

int
RamFSIsDir(const char* __Path__)
{
    if (!__Path__ || !RamFS.Root)
    {
        return 0;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (!Node)
    {
        return 0;
    }

    return (Node->Type == RamFSNode_Directory) ? 1 : 0;
}

int
RamFSIsFile(const char* __Path__)
{
    if (!__Path__ || !RamFS.Root)
    {
        return 0;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (!Node)
    {
        return 0;
    }

    return (Node->Type == RamFSNode_File) ? 1 : 0;
}

uint32_t
RamFSGetSize(const char* __Path__)
{
    if (!__Path__ || !RamFS.Root)
    {
        return 0;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (!Node || Node->Type != RamFSNode_File)
    {
        return 0;
    }

    return Node->Size;
}

uint32_t
RamFSListChildren(RamFSNode* __Dir__, RamFSNode** __Buffer__, uint32_t __MaxCount__)
{
    if (!__Dir__ || !__Buffer__ || __MaxCount__ == 0)
    {
        return 0;
    }

    if (__Dir__->Type != RamFSNode_Directory)
    {
        return 0;
    }

    uint32_t Count = __Dir__->ChildCount;
    if (Count > __MaxCount__)
    {
        Count = __MaxCount__;
    }

    for (uint32_t I = 0; I < Count; I++)
    {
        __Buffer__[I] = __Dir__->Children[I];
    }

    return Count;
}

size_t
RamFSReadFile(const char* __Path__, void* __Buffer__)
{
    if (!__Path__ || !__Buffer__ || !RamFS.Root)
    {
        return 0;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (!Node || Node->Type != RamFSNode_File)
    {
        return 0;
    }

    /*Read the entire file starting at offset 0*/
    return RamFSRead(Node, 0, __Buffer__, (size_t)Node->Size);
}

RamFSNode*
RamFSGetChildByIndex(RamFSNode* __Dir__, uint32_t __Index__)
{
    if (!__Dir__ || __Dir__->Type != RamFSNode_Directory)
    {
        return 0;
    }

    if (__Index__ >= __Dir__->ChildCount)
    {
        return 0;
    }

    return __Dir__->Children[__Index__];
}

char*
RamFSJoinPath(const char* __DirPath__, const char* __Name__)
{
    if (!__DirPath__ || !__Name__)
    {
        return 0;
    }

    /* Compute lengths */
    uint32_t LDir = 0;
    while (__DirPath__[LDir] != '\0')
    {
        LDir++;
    }
    uint32_t LNam = 0;
    while (__Name__[LNam] != '\0')
    {
        LNam++;
    }

    /* Determine if we need a slash */
    int NeedSlash = 1;
    if (LDir > 0 && __DirPath__[LDir - 1] == '/')
    {
        NeedSlash = 0;
    }

    /* Allocate buffer: dir + optional '/' + name + NUL */
    uint32_t Total = LDir + (NeedSlash ? 1 : 0) + LNam + 1;
    char*    Out   = (char*)KMalloc(Total);
    if (!Out)
    {
        return 0;
    }

    /* Copy dir */
    for (uint32_t I = 0; I < LDir; I++)
    {
        Out[I] = __DirPath__[I];
    }
    uint32_t Pos = LDir;

    /* Insert slash if needed */
    if (NeedSlash)
    {
        Out[Pos++] = '/';
    }

    /* Copy name */
    for (uint32_t I = 0; I < LNam; I++)
    {
        Out[Pos++] = __Name__[I];
    }

    /* Terminate */
    Out[Pos] = '\0';
    return Out;
}
