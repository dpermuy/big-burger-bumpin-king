#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>

PPC_FUNC(__imp____C_specific_handler)
{
    fmt::println("[stub] __C_specific_handler(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__DbgBreakPoint)
{
    fmt::println("[stub] DbgBreakPoint(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__IoDismountVolumeByFileHandle)
{
    fmt::println("[stub] IoDismountVolumeByFileHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeBugCheckEx)
{
    fmt::println("[stub] KeBugCheckEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeLockL2)
{
    fmt::println("[stub] KeLockL2(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeReleaseSemaphore)
{
    fmt::println("[stub] KeReleaseSemaphore(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeResetEvent)
{
    fmt::println("[stub] KeResetEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeTlsFree)
{
    fmt::println("[stub] KeTlsFree(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
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

PPC_FUNC(__imp__MmFreePhysicalMemory)
{
    fmt::println("[stub] MmFreePhysicalMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmQueryAddressProtect)
{
    fmt::println("[stub] MmQueryAddressProtect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
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

PPC_FUNC(__imp__NtResumeThread)
{
    fmt::println("[stub] NtResumeThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
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

PPC_FUNC(__imp__RtlCompareMemoryUlong)
{
    fmt::println("[stub] RtlCompareMemoryUlong(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
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

PPC_FUNC(__imp__VdEnableDisableClockGating)
{
    fmt::println("[stub] VdEnableDisableClockGating(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdQueryVideoFlags)
{
    fmt::println("[stub] VdQueryVideoFlags(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
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

