#include <KHeap.h>
#include <RamFs.h>

/**
 * @brief Allocate and initialize a new RamFS node
 *
 * Creates a new file or directory node with default metadata and integrity checks.
 * Initializes all fields to safe defaults and sets the magic number for validation.
 *
 * @param __Name__ NUL-terminated name string (ownership remains with caller)
 * @param __Type__ Node type: RamFSNode_File or RamFSNode_Directory
 * @return Pointer to initialized RamFSNode, or NULL on allocation failure
 *
 * @note The name string is not copied; caller must ensure it remains valid
 * @note Child array is initialized but empty; use RamFSAddChild to populate
 */
RamFSNode*
RamFSCreateNode(const char* __Name__, RamFSNodeType __Type__)
{
    RamFSNode* Node = (RamFSNode*)KMalloc(sizeof(RamFSNode));

    if (!Node)
    {
        return 0;
    }

    Node->Next = 0;                                 /**< Next sibling (unused) */
    for (uint32_t I = 0; I < RamFSMaxChildren; I++) /**< Initialize child array */
    {
        Node->Children[I] = 0;
    }
    Node->ChildCount = 0;              /**< No children initially */
    Node->Name       = __Name__;       /**< Node name */
    Node->Type       = __Type__;       /**< File or directory */
    Node->Size       = 0;              /**< Size (0 for directories) */
    Node->Data       = 0;              /**< Data pointer (NULL for directories) */
    Node->Magic      = RamFSNodeMagic; /**< Integrity check */

    return Node;
}

/**
 * @brief Append a child node to a directory's child list
 *
 * Adds a child node to a directory's child array. The array has a fixed maximum
 * size (RamFSMaxChildren); if the limit is reached, additional children are
 * silently ignored to prevent array overflow.
 *
 * @param __Parent__ Directory node to receive the child (must be RamFSNode_Directory)
 * @param __Child__ Child node to add (file or directory)
 *
 * @note No bounds checking on parent/child validity - caller responsibility
 * @note Fixed-size array prevents unlimited directory growth
 */
void
RamFSAddChild(RamFSNode* __Parent__, RamFSNode* __Child__)
{
    if (!__Parent__ || !__Child__)
    {
        return;
    }

    if (__Parent__->ChildCount < RamFSMaxChildren)
    {
        __Parent__->Children[__Parent__->ChildCount++] = __Child__;
    }
}

/**
 * @brief Ensure the root directory node exists
 *
 * Creates the root directory node ("/") if it doesn't already exist.
 * The root is the top-level directory of the RamFS hierarchy and serves
 * as the mount point for the filesystem.
 *
 * @return Pointer to root directory node, or NULL on allocation failure
 *
 * @note Root name is hardcoded as "/" and not dynamically allocated
 * @note Subsequent calls return the existing root without recreation
 */
RamFSNode*
RamFSEnsureRoot(void)
{
    if (!RamFS.Root)
    {
        /* Root name is "/" (literal string, not allocated) */
        RamFS.Root = RamFSCreateNode("/", RamFSNode_Directory);
    }
    return RamFS.Root;
}
