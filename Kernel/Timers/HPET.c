#include <HPETTimer.h> /* HPET-specific constants and definitions */
#include <Timer.h>     /* Timer management structures */

/** @todo Add HPET Support. */

/** @deprecated Not yet */
int
DetectHpetTimer(void)
{
    return 0;
}

/** @deprecated Not yet */
int
InitializeHpetTimer(void)
{
    PInfo("Initializing HPET Timer...\n");
    return 0;
}
