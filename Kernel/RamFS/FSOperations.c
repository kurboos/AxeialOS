#include <KHeap.h>
#include <RamFs.h>

size_t
RamFSRead(RamFSNode* __Node__, size_t __Offset__, void* __Buffer__, size_t __Length__)
{
    if (Probe_IF_Error(__Node__) || !__Node__ || Probe_IF_Error(__Buffer__) || !__Buffer__)
    {
        return Nothing;
    }

    if (__Node__->Type != RamFSNode_File)
    {
        return Nothing;
    }

    if (__Offset__ >= __Node__->Size)
    {
        return Nothing;
    }

    size_t Available = (size_t)(__Node__->Size) - __Offset__;

    if (__Length__ > Available)
    {
        __Length__ = Available;
    }

    const uint8_t* Src = __Node__->Data + __Offset__;
    uint8_t*       Dst = (uint8_t*)__Buffer__;

    for (size_t I = 0; I < __Length__; I++)
    {
        Dst[I] = Src[I];
    }

    return __Length__;
}

int
RamFSExists(const char* __Path__)
{
    if (Probe_IF_Error(__Path__) || !__Path__ || !RamFS.Root)
    {
        return -BadArgs;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (Probe_IF_Error(Node))
    {
        int Err = Pointer_TO_Error(Node);
        return Err;
    }
    return SysOkay;
}

int
RamFSIsDir(const char* __Path__)
{
    if (Probe_IF_Error(__Path__) || !__Path__ || !RamFS.Root)
    {
        return -NotCanonical;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (Probe_IF_Error(Node) || !Node)
    {
        return -CannotLookup;
    }

    return (Node->Type == RamFSNode_Directory) ? SysOkay : -NoSuch;
}

int
RamFSIsFile(const char* __Path__)
{
    if (Probe_IF_Error(__Path__) || !__Path__ || !RamFS.Root)
    {
        return -NotCanonical;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (Probe_IF_Error(Node) || !Node)
    {
        return -CannotLookup;
    }

    return (Node->Type == RamFSNode_File) ? SysOkay : -NoSuch;
}

uint32_t
RamFSGetSize(const char* __Path__)
{
    if (Probe_IF_Error(__Path__) || !__Path__ || !RamFS.Root)
    {
        return Nothing;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (Probe_IF_Error(Node) || !Node || Node->Type != RamFSNode_File)
    {
        return Nothing;
    }

    return Node->Size;
}

uint32_t
RamFSListChildren(RamFSNode* __Dir__, RamFSNode** __Buffer__, uint32_t __MaxCount__)
{
    if (Probe_IF_Error(__Dir__) || !__Dir__ || Probe_IF_Error(__Buffer__) || !__Buffer__ ||
        __MaxCount__ == 0)
    {
        return Nothing;
    }

    if (__Dir__->Type != RamFSNode_Directory)
    {
        return Nothing;
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
    if (Probe_IF_Error(__Path__) || !__Path__ || Probe_IF_Error(__Buffer__) || !__Buffer__ ||
        !RamFS.Root)
    {
        return Nothing;
    }

    RamFSNode* Node = RamFSLookup(RamFS.Root, __Path__);
    if (Probe_IF_Error(Node) || !Node || Node->Type != RamFSNode_File)
    {
        return Nothing;
    }

    /*Read the entire file starting at offset 0*/
    return RamFSRead(Node, 0, __Buffer__, (size_t)Node->Size);
}

RamFSNode*
RamFSGetChildByIndex(RamFSNode* __Dir__, uint32_t __Index__)
{
    if (Probe_IF_Error(__Dir__) || !__Dir__ || __Dir__->Type != RamFSNode_Directory)
    {
        return Error_TO_Pointer(-BadEntity);
    }

    if (__Index__ >= __Dir__->ChildCount)
    {
        return Error_TO_Pointer(-TooMany);
    }

    return __Dir__->Children[__Index__];
}

char*
RamFSJoinPath(const char* __DirPath__, const char* __Name__)
{
    if (Probe_IF_Error(__DirPath__) || !__DirPath__ || Probe_IF_Error(__Name__) || !__Name__)
    {
        return Error_TO_Pointer(-BadArgs);
    }

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

    int NeedSlash = 1;
    if (LDir > 0 && __DirPath__[LDir - 1] == '/')
    {
        NeedSlash = 0;
    }

    /* Allocate buffer: dir + optional '/' + name + NUL */
    uint32_t Total = LDir + (NeedSlash ? 1 : 0) + LNam + 1;
    char*    Out   = (char*)KMalloc(Total);
    if (Probe_IF_Error(Out) || !Out)
    {
        return Error_TO_Pointer(-BadArgs);
    }

    /* Copy dir */
    for (uint32_t I = 0; I < LDir; I++)
    {
        Out[I] = __DirPath__[I];
    }
    uint32_t Pos = LDir;

    if (NeedSlash)
    {
        Out[Pos++] = '/';
    }

    /* Copy name */
    for (uint32_t I = 0; I < LNam; I++)
    {
        Out[Pos++] = __Name__[I];
    }

    Out[Pos] = '\0';
    return Out;
}
