#include <AllTypes.h>
#include <KExports.h>
#include <KrnPrintf.h>
#include <ModELF.h>
#include <String.h>
#include <VFS.h>

#define MaxDevModules 128

/**
 * @brief Initialize all device drivers by scanning (RamFS) root for .ko modules.
 *        Each .ko file is passed to InstallModule() for loading in numeric order.
 *        Warns if a module lacks a numeric prefix, and also warns if sequence gaps exist.
 */
void
InitRamDiskDevDrvs(void)
{
    VfsDirEnt Entries[MaxDevModules];
    long      Count = VfsReaddir("/", (void*)Entries, MaxDevModules);
    if (Count < 0)
    {
        PError("InitDevDrvs: cannot read root directory %lx\n", (int)Count);
        return;
    }

    struct ModuleEntry
    {
        char Path[256];
        long Seq;

    } Mods[MaxDevModules];

    long ModCount = 0;

    /* Collect modules */
    for (long I = 0; I < Count; I++)
    {
        const char* Name = Entries[I].Name;
        long        Len  = strlen(Name);

        if (Len > 3 && strcmp(Name + Len - 3, ".ko") == 0)
        {
            char Path[256];
            if (VfsJoinPath("" /*VFS sets it to root.*/, Name, Path, sizeof(Path)) < 0)
            {
                PError("InitDevDrvs: join path failed for %s\n", Name);
                continue;
            }

            /* Extract numeric prefix */
            char NumBuf[32];
            long J = 0;
            while (Name[J] >= '0' && Name[J] <= '9' && J < (long)sizeof(NumBuf) - 1)
            {
                NumBuf[J] = Name[J];
                J++;
            }
            NumBuf[J] = 0;

            long Seq = -1;
            if (J > 0)
            {
                Seq = atol(NumBuf);
            }
            else
            {
                PWarn("InitDevDrvs: module %s has no numeric prefix\n", Name);
            }

            strncpy(Mods[ModCount].Path, Path, sizeof(Mods[ModCount].Path));
            Mods[ModCount].Seq = Seq;
            ModCount++;
        }
    }

    /* Sort by Seq ascending */
    for (long I = 0; I < ModCount - 1; I++)
    {
        for (long J = I + 1; J < ModCount; J++)
        {
            if (Mods[I].Seq < 0 || (Mods[J].Seq >= 0 && Mods[J].Seq < Mods[I].Seq))
            {
                struct ModuleEntry Tmp = Mods[I];
                Mods[I]                = Mods[J];
                Mods[J]                = Tmp;
            }
        }
    }

    /* Validate gaps and load */
    long Expected = (ModCount > 0 && Mods[0].Seq > 0) ? Mods[0].Seq : 1;

    for (long I = 0; I < ModCount; I++)
    {
        if (Mods[I].Seq >= 0)
        {
            if (Mods[I].Seq != Expected)
            {
                PWarn("InitDevDrvs: expected module prefix %ld but found %ld\n",
                      Expected,
                      Mods[I].Seq);
                /* Adjust expected to current */
                Expected = Mods[I].Seq;
            }
            Expected++;
        }

        PInfo("InitDevDrvs: loading module %s\n", Mods[I].Path);
        if (InstallModule(Mods[I].Path) < 0)
        {
            PError("InitDevDrvs: failed to install %s\n", Mods[I].Path);
        }
    }
}