#include <CharBus.h>
#include <KHeap.h>
#include <KrnPrintf.h>

/** @section Char bus layer */

/**
 * @brief Open a character device via CharBus shim.
 *
 * This function is invoked by DevFS when a char device node is opened.
 * It resolves the CharBus descriptor from the context pointer, validates
 * the descriptor, and delegates to the driver-provided Open handler.
 *
 * @param __Ctx__ Device-private context pointer (CharBus*).
 *
 * @return Result code from the driver Open handler,
 *         -1 if the descriptor is invalid,
 *          0 if the Open handler is missing.
 */
static int
CharBusOpen(void* __Ctx__)
{
    CharBus* B = (CharBus*)__Ctx__;
    PDebug("CHAR: Open ctx=%p name=%s drvOpen=%p drvCtx=%p\n",
           __Ctx__,
           B ? B->Name : "(nil)",
           B ? (void*)B->Ops.Open : 0,
           B ? B->CtrlCtx : 0);
    if (!B || !B->Name || !B->CtrlCtx)
    {
        PError("CHAR: Open invalid ctx\n");
        return -1;
    }
    if (!B->Ops.Open)
    {
        PWarn("CHAR: Open op missing\n");
        return 0;
    }
    int Rc = B->Ops.Open(B->CtrlCtx);
    PDebug("CHAR: Open -> rc=%d\n", Rc);
    return Rc;
}

/**
 * @brief Close a character device via CharBus shim.
 *
 * This function is invoked by DevFS when a char device node is closed.
 * It resolves the CharBus descriptor, validates it, and delegates to
 * the driver-provided Close handler.
 *
 * @param __Ctx__ Device-private context pointer (CharBus*).
 *
 * @return Result code from the driver Close handler,
 *         -1 if the descriptor is invalid,
 *          0 if the Close handler is missing.
 */
static int
CharBusClose(void* __Ctx__)
{
    CharBus* B = (CharBus*)__Ctx__;
    PDebug("CHAR: Close ctx=%p name=%s drvClose=%p drvCtx=%p\n",
           __Ctx__,
           B ? B->Name : "(nil)",
           B ? (void*)B->Ops.Close : 0,
           B ? B->CtrlCtx : 0);
    if (!B || !B->Name || !B->CtrlCtx)
    {
        PError("CHAR: Close invalid ctx\n");
        return -1;
    }
    if (!B->Ops.Close)
    {
        PWarn("CHAR: Close op missing\n");
        return 0;
    }
    int Rc = B->Ops.Close(B->CtrlCtx);
    PDebug("CHAR: Close -> rc=%d\n", Rc);
    return Rc;
}

/**
 * @brief Read from a character device via CharBus shim.
 *
 * This function is invoked by DevFS when a read operation is performed
 * on a char device node. It validates the descriptor and buffer, then
 * delegates to the driver-provided Read handler.
 *
 * @param __Ctx__ Device-private context pointer (CharBus*).
 * @param __Buf__ Destination buffer for read data.
 * @param __Len__ Maximum number of bytes to read.
 *
 * @return Number of bytes read (>=0),
 *         0 if invalid arguments or handler missing.
 */
static long
CharBusRead(void* __Ctx__, void* __Buf__, long __Len__)
{
    CharBus* B = (CharBus*)__Ctx__;
    PDebug("CHAR: Read ctx=%p name=%s buf=%p len=%ld drvRead=%p drvCtx=%p\n",
           __Ctx__,
           B ? B->Name : "(nil)",
           __Buf__,
           __Len__,
           B ? (void*)B->Ops.Read : 0,
           B ? B->CtrlCtx : 0);
    if (!B || !B->Name || !B->CtrlCtx || !__Buf__ || __Len__ <= 0)
    {
        PError("CHAR: Read invalid args\n");
        return 0;
    }
    if (!B->Ops.Read)
    {
        PWarn("CHAR: Read op missing\n");
        return 0;
    }
    long Got = B->Ops.Read(B->CtrlCtx, __Buf__, __Len__);
    PDebug("CHAR: Read -> got=%ld\n", Got);
    return (Got < 0) ? 0 : Got;
}

/**
 * @brief Write to a character device via CharBus shim.
 *
 * This function is invoked by DevFS when a write operation is performed
 * on a char device node. It validates the descriptor and buffer, then
 * delegates to the driver-provided Write handler.
 *
 * @param __Ctx__ Device-private context pointer (CharBus*).
 * @param __Buf__ Source buffer containing data to write.
 * @param __Len__ Number of bytes to write.
 *
 * @return Number of bytes written (>=0),
 *         -1 if invalid arguments or handler missing.
 */
static long
CharBusWrite(void* __Ctx__, const void* __Buf__, long __Len__)
{
    CharBus* B = (CharBus*)__Ctx__;
    PDebug("CHAR: Write ctx=%p name=%s buf=%p len=%ld drvWrite=%p drvCtx=%p\n",
           __Ctx__,
           B ? B->Name : "(nil)",
           __Buf__,
           __Len__,
           B ? (void*)B->Ops.Write : 0,
           B ? B->CtrlCtx : 0);
    if (!B || !B->Name || !B->CtrlCtx || !__Buf__ || __Len__ <= 0)
    {
        PError("CHAR: Write invalid args\n");
        return -1;
    }
    if (!B->Ops.Write)
    {
        PWarn("CHAR: Write op missing\n");
        return -1;
    }
    long Put = B->Ops.Write(B->CtrlCtx, __Buf__, __Len__);
    PDebug("CHAR: Write -> put=%ld\n", Put);
    return (Put < 0) ? -1 : Put;
}

/**
 * @brief Perform an ioctl on a character device via CharBus shim.
 *
 * This function is invoked by DevFS when an ioctl operation is performed
 * on a char device node. It validates the descriptor, then delegates to
 * the driver-provided Ioctl handler.
 *
 * @param __Ctx__ Device-private context pointer (CharBus*).
 * @param __Cmd__ Ioctl command code.
 * @param __Arg__ Optional argument pointer for ioctl.
 *
 * @return Result code from the driver Ioctl handler,
 *         -1 if the descriptor is invalid,
 *          0 if the Ioctl handler is missing.
 */
static int
CharBusIoctl(void* __Ctx__, unsigned long __Cmd__, void* __Arg__)
{
    CharBus* B = (CharBus*)__Ctx__;
    PDebug("CHAR: Ioctl ctx=%p name=%s cmd=0x%lx drvIoctl=%p drvCtx=%p\n",
           __Ctx__,
           B ? B->Name : "(nil)",
           __Cmd__,
           B ? (void*)B->Ops.Ioctl : 0,
           B ? B->CtrlCtx : 0);
    if (!B || !B->Name || !B->CtrlCtx)
    {
        PError("CHAR: Ioctl invalid ctx\n");
        return -1;
    }
    if (!B->Ops.Ioctl)
    {
        PWarn("CHAR: Ioctl op missing\n");
        return 0;
    }
    int Rc = B->Ops.Ioctl(B->CtrlCtx, __Cmd__, __Arg__);
    PDebug("CHAR: Ioctl -> rc=%d\n", Rc);
    return Rc;
}

/** @section Main Shit */

/**
 * @brief Register a character device with DevFS.
 *
 * Wraps a CharBus descriptor into a CharDevOps shim and registers it
 * with DevFS as a char device node. DevFS stores the CharBus pointer
 * as device-private context.
 *
 * @param __Bus__   Pointer to the CharBus descriptor (must be permanent memory).
 * @param __Major__ Major device number to assign.
 * @param __Minor__ Minor device number to assign.
 *
 * @return 0 on success,
 *         negative error code on failure.
 *
 * @note The CharBus descriptor must remain valid for the lifetime of
 *       the device node. Do not pass stack-allocated descriptors.
 */
int
CharRegisterBus(CharBus* __Bus__, int __Major__, int __Minor__)
{
    if (!__Bus__ || !__Bus__->Name || !__Bus__->CtrlCtx)
    {
        PError("CHAR: Invalid bus descriptor Name=%p CtrlCtx=%p\n",
               (void*)(__Bus__ ? __Bus__->Name : 0),
               __Bus__ ? __Bus__->CtrlCtx : 0);
        return -1;
    }
    if (!__Bus__->Ops.Open || !__Bus__->Ops.Close || !__Bus__->Ops.Read || !__Bus__->Ops.Write ||
        !__Bus__->Ops.Ioctl)
    {
        PError("CHAR: Ops table incomplete O:%p C:%p R:%p W:%p I:%p\n",
               (void*)__Bus__->Ops.Open,
               (void*)__Bus__->Ops.Close,
               (void*)__Bus__->Ops.Read,
               (void*)__Bus__->Ops.Write,
               (void*)__Bus__->Ops.Ioctl);
        return -1;
    }

    PDebug("CHAR: Register bus=%p name=%s drvCtx=%p opsR=%p opsW=%p opsO=%p opsC=%p opsI=%p\n",
           __Bus__,
           __Bus__->Name,
           __Bus__->CtrlCtx,
           (void*)__Bus__->Ops.Read,
           (void*)__Bus__->Ops.Write,
           (void*)__Bus__->Ops.Open,
           (void*)__Bus__->Ops.Close,
           (void*)__Bus__->Ops.Ioctl);

    CharDevOps Ops = {.Open  = CharBusOpen,
                      .Close = CharBusClose,
                      .Read  = CharBusRead,
                      .Write = CharBusWrite,
                      .Ioctl = CharBusIoctl};

    int Res = DevFsRegisterCharDevice(__Bus__->Name, __Major__, __Minor__, Ops, (void*)__Bus__);
    PDebug("CHAR: DevFsRegisterCharDevice -> rc=%d\n", Res);

    if (Res != 0)
    {
        PError("CHAR: register %s failed (%d)\n", __Bus__->Name, Res);
        return Res;
    }

    PInfo("CHAR: /dev/%s ready (major=%d, minor=%d)\n", __Bus__->Name, __Major__, __Minor__);
    return 0;
}