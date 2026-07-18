#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

PPC_FUNC(__imp____C_specific_handler)
{
    fmt::println("[stub] __C_specific_handler(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp___vsnprintf)
{
    fmt::println("[stub] _vsnprintf(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__DbgBreakPoint)
{
    fmt::println("[stub] DbgBreakPoint(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__DbgPrint)
{
    fmt::println("[stub] DbgPrint(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExCreateThread)
{
    fmt::println("[stub] ExCreateThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    // Deliberate Phase 2A scope cut: reports success but does not spawn a
    // real host thread or execute the requested guest function. Real
    // thread semantics are Phase 2B work.
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExRegisterTitleTerminateNotification)
{
    fmt::println("[stub] ExRegisterTitleTerminateNotification(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ExTerminateThread)
{
    fmt::println("[stub] ExTerminateThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__IoDismountVolumeByFileHandle)
{
    fmt::println("[stub] IoDismountVolumeByFileHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql)
{
    fmt::println("[stub] KeAcquireSpinLockAtRaisedIrql(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeBugCheckEx)
{
    fmt::println("[stub] KeBugCheckEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeDelayExecutionThread)
{
    fmt::println("[stub] KeDelayExecutionThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeEnterCriticalRegion)
{
    fmt::println("[stub] KeEnterCriticalRegion(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeInitializeSemaphore)
{
    fmt::println("[stub] KeInitializeSemaphore(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeLeaveCriticalRegion)
{
    fmt::println("[stub] KeLeaveCriticalRegion(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeLockL2)
{
    fmt::println("[stub] KeLockL2(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeRaiseIrqlToDpcLevel)
{
    fmt::println("[stub] KeRaiseIrqlToDpcLevel(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeReleaseSemaphore)
{
    fmt::println("[stub] KeReleaseSemaphore(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql)
{
    fmt::println("[stub] KeReleaseSpinLockFromRaisedIrql(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeResetEvent)
{
    fmt::println("[stub] KeResetEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeResumeThread)
{
    fmt::println("[stub] KeResumeThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeSetAffinityThread)
{
    fmt::println("[stub] KeSetAffinityThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeSetBasePriorityThread)
{
    fmt::println("[stub] KeSetBasePriorityThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeSetEvent)
{
    fmt::println("[stub] KeSetEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeTlsFree)
{
    fmt::println("[stub] KeTlsFree(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeTlsGetValue)
{
    fmt::println("[stub] KeTlsGetValue(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeUnlockL2)
{
    fmt::println("[stub] KeUnlockL2(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeWaitForMultipleObjects)
{
    fmt::println("[stub] KeWaitForMultipleObjects(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeWaitForSingleObject)
{
    fmt::println("[stub] KeWaitForSingleObject(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KfAcquireSpinLock)
{
    fmt::println("[stub] KfAcquireSpinLock(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KfLowerIrql)
{
    fmt::println("[stub] KfLowerIrql(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KfReleaseSpinLock)
{
    fmt::println("[stub] KfReleaseSpinLock(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KiApcNormalRoutineNop)
{
    fmt::println("[stub] KiApcNormalRoutineNop(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmFreePhysicalMemory)
{
    fmt::println("[stub] MmFreePhysicalMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmGetPhysicalAddress)
{
    fmt::println("[stub] MmGetPhysicalAddress(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmQueryAddressProtect)
{
    fmt::println("[stub] MmQueryAddressProtect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmSetAddressProtect)
{
    fmt::println("[stub] MmSetAddressProtect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll___WSAFDIsSet)
{
    fmt::println("[stub] NetDll___WSAFDIsSet(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_accept)
{
    fmt::println("[stub] NetDll_accept(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_bind)
{
    fmt::println("[stub] NetDll_bind(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_closesocket)
{
    fmt::println("[stub] NetDll_closesocket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_connect)
{
    fmt::println("[stub] NetDll_connect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_getsockname)
{
    fmt::println("[stub] NetDll_getsockname(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_inet_addr)
{
    fmt::println("[stub] NetDll_inet_addr(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_ioctlsocket)
{
    fmt::println("[stub] NetDll_ioctlsocket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_listen)
{
    fmt::println("[stub] NetDll_listen(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_recvfrom)
{
    fmt::println("[stub] NetDll_recvfrom(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_select)
{
    fmt::println("[stub] NetDll_select(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_send)
{
    fmt::println("[stub] NetDll_send(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_sendto)
{
    fmt::println("[stub] NetDll_sendto(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_setsockopt)
{
    fmt::println("[stub] NetDll_setsockopt(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_shutdown)
{
    fmt::println("[stub] NetDll_shutdown(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_socket)
{
    fmt::println("[stub] NetDll_socket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSACleanup)
{
    fmt::println("[stub] NetDll_WSACleanup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSAGetLastError)
{
    fmt::println("[stub] NetDll_WSAGetLastError(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSAStartup)
{
    fmt::println("[stub] NetDll_WSAStartup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetCleanup)
{
    fmt::println("[stub] NetDll_XNetCleanup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetConnect)
{
    fmt::println("[stub] NetDll_XNetConnect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetGetConnectStatus)
{
    fmt::println("[stub] NetDll_XNetGetConnectStatus(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetGetEthernetLinkStatus)
{
    fmt::println("[stub] NetDll_XNetGetEthernetLinkStatus(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetGetTitleXnAddr)
{
    fmt::println("[stub] NetDll_XNetGetTitleXnAddr(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetQosListen)
{
    fmt::println("[stub] NetDll_XNetQosListen(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetQosLookup)
{
    fmt::println("[stub] NetDll_XNetQosLookup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetQosRelease)
{
    fmt::println("[stub] NetDll_XNetQosRelease(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetStartup)
{
    fmt::println("[stub] NetDll_XNetStartup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetXnAddrToInAddr)
{
    fmt::println("[stub] NetDll_XNetXnAddrToInAddr(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtCancelTimer)
{
    fmt::println("[stub] NtCancelTimer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtClearEvent)
{
    fmt::println("[stub] NtClearEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtCreateTimer)
{
    fmt::println("[stub] NtCreateTimer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtDuplicateObject)
{
    fmt::println("[stub] NtDuplicateObject(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtFlushBuffersFile)
{
    fmt::println("[stub] NtFlushBuffersFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtFreeVirtualMemory)
{
    fmt::println("[stub] NtFreeVirtualMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryDirectoryFile)
{
    fmt::println("[stub] NtQueryDirectoryFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryFullAttributesFile)
{
    fmt::println("[stub] NtQueryFullAttributesFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryInformationFile)
{
    fmt::println("[stub] NtQueryInformationFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryVirtualMemory)
{
    fmt::println("[stub] NtQueryVirtualMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryVolumeInformationFile)
{
    fmt::println("[stub] NtQueryVolumeInformationFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueueApcThread)
{
    fmt::println("[stub] NtQueueApcThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtResumeThread)
{
    fmt::println("[stub] NtResumeThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtSetEvent)
{
    fmt::println("[stub] NtSetEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtSetInformationFile)
{
    fmt::println("[stub] NtSetInformationFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtSetTimerEx)
{
    fmt::println("[stub] NtSetTimerEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtSuspendThread)
{
    fmt::println("[stub] NtSuspendThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ObCreateSymbolicLink)
{
    fmt::println("[stub] ObCreateSymbolicLink(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ObDeleteSymbolicLink)
{
    fmt::println("[stub] ObDeleteSymbolicLink(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ObDereferenceObject)
{
    fmt::println("[stub] ObDereferenceObject(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ObReferenceObjectByHandle)
{
    fmt::println("[stub] ObReferenceObjectByHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlCompareMemoryUlong)
{
    fmt::println("[stub] RtlCompareMemoryUlong(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlFillMemoryUlong)
{
    fmt::println("[stub] RtlFillMemoryUlong(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlImageXexHeaderField)
{
    fmt::println("[stub] RtlImageXexHeaderField(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlMultiByteToUnicodeN)
{
    fmt::println("[stub] RtlMultiByteToUnicodeN(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlRaiseException)
{
    fmt::println("[stub] RtlRaiseException(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlTimeFieldsToTime)
{
    fmt::println("[stub] RtlTimeFieldsToTime(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlTryEnterCriticalSection)
{
    fmt::println("[stub] RtlTryEnterCriticalSection(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlUnicodeToMultiByteN)
{
    fmt::println("[stub] RtlUnicodeToMultiByteN(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlUnwind)
{
    fmt::println("[stub] RtlUnwind(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__sprintf)
{
    fmt::println("[stub] sprintf(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__StfsControlDevice)
{
    fmt::println("[stub] StfsControlDevice(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__StfsCreateDevice)
{
    fmt::println("[stub] StfsCreateDevice(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdCallGraphicsNotificationRoutines)
{
    fmt::println("[stub] VdCallGraphicsNotificationRoutines(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdEnableDisableClockGating)
{
    fmt::println("[stub] VdEnableDisableClockGating(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack)
{
    fmt::println("[stub] VdEnableRingBufferRPtrWriteBack(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdGetCurrentDisplayGamma)
{
    fmt::println("[stub] VdGetCurrentDisplayGamma(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdGetCurrentDisplayInformation)
{
    fmt::println("[stub] VdGetCurrentDisplayInformation(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdGetSystemCommandBuffer)
{
    fmt::println("[stub] VdGetSystemCommandBuffer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdInitializeEngines)
{
    fmt::println("[stub] VdInitializeEngines(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdInitializeRingBuffer)
{
    fmt::println("[stub] VdInitializeRingBuffer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdInitializeScalerCommandBuffer)
{
    fmt::println("[stub] VdInitializeScalerCommandBuffer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdIsHSIOTrainingSucceeded)
{
    fmt::println("[stub] VdIsHSIOTrainingSucceeded(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdPersistDisplay)
{
    fmt::println("[stub] VdPersistDisplay(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdQueryVideoFlags)
{
    fmt::println("[stub] VdQueryVideoFlags(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdQueryVideoMode)
{
    fmt::println("[stub] VdQueryVideoMode(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdRetrainEDRAM)
{
    fmt::println("[stub] VdRetrainEDRAM(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdRetrainEDRAMWorker)
{
    fmt::println("[stub] VdRetrainEDRAMWorker(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdSetDisplayMode)
{
    fmt::println("[stub] VdSetDisplayMode(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdSetGraphicsInterruptCallback)
{
    fmt::println("[stub] VdSetGraphicsInterruptCallback(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress)
{
    fmt::println("[stub] VdSetSystemCommandBufferGpuIdentifierAddress(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdShutdownEngines)
{
    fmt::println("[stub] VdShutdownEngines(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdSwap)
{
    fmt::println("[stub] VdSwap(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamAlloc)
{
    fmt::println("[stub] XamAlloc(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentClose)
{
    fmt::println("[stub] XamContentClose(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentCreateEx)
{
    fmt::println("[stub] XamContentCreateEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentGetDeviceData)
{
    fmt::println("[stub] XamContentGetDeviceData(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentSetThumbnail)
{
    fmt::println("[stub] XamContentSetThumbnail(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamEnumerate)
{
    fmt::println("[stub] XamEnumerate(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamFree)
{
    fmt::println("[stub] XamFree(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamGetExecutionId)
{
    fmt::println("[stub] XamGetExecutionId(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamGetSystemVersion)
{
    fmt::println("[stub] XamGetSystemVersion(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamInputGetCapabilities)
{
    fmt::println("[stub] XamInputGetCapabilities(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamInputGetState)
{
    fmt::println("[stub] XamInputGetState(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamInputSetState)
{
    fmt::println("[stub] XamInputSetState(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderGetLaunchData)
{
    fmt::println("[stub] XamLoaderGetLaunchData(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderGetLaunchDataSize)
{
    fmt::println("[stub] XamLoaderGetLaunchDataSize(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderLaunchTitle)
{
    fmt::println("[stub] XamLoaderLaunchTitle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderSetLaunchData)
{
    fmt::println("[stub] XamLoaderSetLaunchData(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderTerminateTitle)
{
    fmt::println("[stub] XamLoaderTerminateTitle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamNotifyCreateListener)
{
    fmt::println("[stub] XamNotifyCreateListener(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamSessionCreateHandle)
{
    fmt::println("[stub] XamSessionCreateHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamSessionRefObjByHandle)
{
    fmt::println("[stub] XamSessionRefObjByHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowAchievementsUI)
{
    fmt::println("[stub] XamShowAchievementsUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowDeviceSelectorUI)
{
    fmt::println("[stub] XamShowDeviceSelectorUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowFriendsUI)
{
    fmt::println("[stub] XamShowFriendsUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowGamerCardUIForXUID)
{
    fmt::println("[stub] XamShowGamerCardUIForXUID(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowMessageBoxUIEx)
{
    fmt::println("[stub] XamShowMessageBoxUIEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowPlayerReviewUI)
{
    fmt::println("[stub] XamShowPlayerReviewUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowSigninUI)
{
    fmt::println("[stub] XamShowSigninUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamTaskCloseHandle)
{
    fmt::println("[stub] XamTaskCloseHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamTaskSchedule)
{
    fmt::println("[stub] XamTaskSchedule(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamTaskShouldExit)
{
    fmt::println("[stub] XamTaskShouldExit(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserAreUsersFriends)
{
    fmt::println("[stub] XamUserAreUsersFriends(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserCheckPrivilege)
{
    fmt::println("[stub] XamUserCheckPrivilege(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserCreateStatsEnumerator)
{
    fmt::println("[stub] XamUserCreateStatsEnumerator(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetName)
{
    fmt::println("[stub] XamUserGetName(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetSigninState)
{
    fmt::println("[stub] XamUserGetSigninState(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetXUID)
{
    fmt::println("[stub] XamUserGetXUID(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserReadProfileSettings)
{
    fmt::println("[stub] XamUserReadProfileSettings(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserWriteProfileSettings)
{
    fmt::println("[stub] XamUserWriteProfileSettings(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceClose)
{
    fmt::println("[stub] XamVoiceClose(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceCreate)
{
    fmt::println("[stub] XamVoiceCreate(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceHeadsetPresent)
{
    fmt::println("[stub] XamVoiceHeadsetPresent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceSubmitPacket)
{
    fmt::println("[stub] XamVoiceSubmitPacket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioGetVoiceCategoryVolume)
{
    fmt::println("[stub] XAudioGetVoiceCategoryVolume(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioGetVoiceCategoryVolumeChangeMask)
{
    fmt::println("[stub] XAudioGetVoiceCategoryVolumeChangeMask(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioRegisterRenderDriverClient)
{
    fmt::println("[stub] XAudioRegisterRenderDriverClient(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioSubmitRenderDriverFrame)
{
    fmt::println("[stub] XAudioSubmitRenderDriverFrame(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioUnregisterRenderDriverClient)
{
    fmt::println("[stub] XAudioUnregisterRenderDriverClient(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XeCryptSha)
{
    fmt::println("[stub] XeCryptSha(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XeKeysConsolePrivateKeySign)
{
    fmt::println("[stub] XeKeysConsolePrivateKeySign(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XeKeysConsoleSignatureVerification)
{
    fmt::println("[stub] XeKeysConsoleSignatureVerification(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XexGetModuleHandle)
{
    fmt::println("[stub] XexGetModuleHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XexGetModuleSection)
{
    fmt::println("[stub] XexGetModuleSection(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XexGetProcedureAddress)
{
    fmt::println("[stub] XexGetProcedureAddress(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetGameRegion)
{
    fmt::println("[stub] XGetGameRegion(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetLanguage)
{
    fmt::println("[stub] XGetLanguage(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetVideoMode)
{
    fmt::println("[stub] XGetVideoMode(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMACreateContext)
{
    fmt::println("[stub] XMACreateContext(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAReleaseContext)
{
    fmt::println("[stub] XMAReleaseContext(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgCancelIORequest)
{
    fmt::println("[stub] XMsgCancelIORequest(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgInProcessCall)
{
    fmt::println("[stub] XMsgInProcessCall(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgStartIORequest)
{
    fmt::println("[stub] XMsgStartIORequest(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XNotifyGetNext)
{
    fmt::println("[stub] XNotifyGetNext(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

