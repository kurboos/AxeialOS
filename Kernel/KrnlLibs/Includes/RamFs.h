#pragma once

#include <AllTypes.h>
#include <Errnos.h>
#include <KrnPrintf.h>
#include <PMM.h>
#include <VFS.h>
#include <VMM.h>

#define RamFSMaxChildren 64
#define RamFSMagic       0xCAFEBABE
#define RamFSNodeMagic   0xBAADF00D

/* cpio "newc" specifics */
#define CpioNewcMagic "070701"
#define CpioAlign     4
#define CpioTrailer   "TRAILER!!!"

typedef enum RamFSNodeType
{
    RamFSNode_File,
    RamFSNode_Directory

} RamFSNodeType;

typedef struct RamFSNode
{
    struct RamFSNode* Next;                       /* Sibling linkage */
    struct RamFSNode* Children[RamFSMaxChildren]; /* Child array */
    uint32_t          ChildCount;                 /* Child count */
    const char*       Name;                       /* Entry name */
    RamFSNodeType     Type;                       /* File or Directory */
    uint32_t          Size;                       /* File size */
    const uint8_t*    Data;                       /* File data pointer */
    uint32_t          Magic;                      /* Integrity glyph */

} RamFSNode;

typedef struct
{
    RamFSNode* Root;  /* Root node */
    uint32_t   Magic; /* Ritual validation */

} RamFSContext;

extern RamFSContext RamFS;

uint32_t   CpioAlignUp(uint32_t __Value__, uint32_t __Align__);
uint32_t   CpioParseHex(const char* __Hex__);
RamFSNode* RamFSCreateNode(const char* __Name__, RamFSNodeType __Type__);
void       RamFSAddChild(RamFSNode* __Parent__, RamFSNode* __Child__, SysErr* __Err__);
RamFSNode* RamFSEnsureRoot(void);
RamFSNode* RamFSAttachPath(RamFSNode*     __Root__,
                           const char*    __FullPath__,
                           RamFSNodeType  __Type__,
                           const uint8_t* __Data__,
                           uint32_t       __Size__);
RamFSNode* RamFSMount(const void* __Image__, size_t __Length__);
RamFSNode* RamFSLookup(RamFSNode* __Root__, const char* __Path__);
size_t     RamFSRead(RamFSNode* __Node__, size_t __Offset__, void* __Buffer__, size_t __Length__);
int        RamFSExists(const char* __Path__);
int        RamFSIsDir(const char* __Path__);
int        RamFSIsFile(const char* __Path__);
uint32_t   RamFSGetSize(const char* __Path__);
uint32_t   RamFSListChildren(RamFSNode* __Dir__, RamFSNode** __Buffer__, uint32_t __MaxCount);
size_t     RamFSReadFile(const char* __Path__, void* __Buffer__);
char*      RamFSJoinPath(const char* __DirPath__, const char* __Name__);

typedef struct RamVfsPrivNode
{
    RamFSNode* Node;

} RamVfsPrivNode;

typedef struct RamVfsPrivFile
{
    RamFSNode* Node;
    long       Offset;

} RamVfsPrivFile;

int    RamVfsOpen(Vnode*, File*);
int    RamVfsClose(File*);
long   RamVfsRead(File*, void*, long);
long   RamVfsWrite(File*, const void*, long);
long   RamVfsLseek(File*, long, int);
int    RamVfsIoctl(File*, unsigned long, void*);
int    RamVfsStat(Vnode*, VfsStat*);
long   RamVfsReaddir(Vnode*, void*, long);
Vnode* RamVfsLookup(Vnode*, const char*);
int    RamVfsCreate(Vnode*, const char*, long, VfsPerm);
int    RamVfsUnlink(Vnode*, const char*);
int    RamVfsMkdir(Vnode*, const char*, VfsPerm);
int    RamVfsRmdir(Vnode*, const char*);
int    RamVfsSymlink(Vnode*, const char*, const char*, VfsPerm);
int    RamVfsReadlink(Vnode*, VfsNameBuf*);
int    RamVfsLink(Vnode*, Vnode*, const char*);
int    RamVfsRename(Vnode*, const char*, Vnode*, const char*, long);
int    RamVfsChmod(Vnode*, long);
int    RamVfsChown(Vnode*, long, long);
int    RamVfsTruncate(Vnode*, long);
int    RamVfsSync(Vnode*);
int    RamVfsMap(Vnode*, void**, long, long);
int    RamVfsUnmap(Vnode*, void*, long);

int  RamVfsSuperSync(Superblock*);
int  RamVfsSuperStatFs(Superblock*, VfsStatFs*);
void RamVfsSuperRelease(Superblock*, SysErr*);
int  RamVfsSuperUmount(Superblock*);

Superblock* RamFsMountImpl(const char*, const char*);
int         BootMountRamFs(const void* __Initrd__, size_t __Len__);

extern const VnodeOps __RamVfsOps__;
extern const SuperOps __RamVfsSuperOps__;