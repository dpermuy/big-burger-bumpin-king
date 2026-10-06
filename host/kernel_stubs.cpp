#include "ppc_config.h"
#include <ppc_context.h>
#include <fmt/core.h>
#include "host_print.h"

PPC_FUNC(__imp____C_specific_handler)
{
    HostPrintln("[stub] __C_specific_handler(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__DbgBreakPoint)
{
    HostPrintln("[stub] DbgBreakPoint(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__IoDismountVolumeByFileHandle)
{
    HostPrintln("[stub] IoDismountVolumeByFileHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeBugCheckEx)
{
    HostPrintln("[stub] KeBugCheckEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeLockL2)
{
    HostPrintln("[stub] KeLockL2(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeReleaseSemaphore)
{
    HostPrintln("[stub] KeReleaseSemaphore(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeResetEvent)
{
    HostPrintln("[stub] KeResetEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeTlsFree)
{
    HostPrintln("[stub] KeTlsFree(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeUnlockL2)
{
    HostPrintln("[stub] KeUnlockL2(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeWaitForMultipleObjects)
{
    HostPrintln("[stub] KeWaitForMultipleObjects(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmFreePhysicalMemory)
{
    HostPrintln("[stub] MmFreePhysicalMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__MmQueryAddressProtect)
{
    HostPrintln("[stub] MmQueryAddressProtect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll___WSAFDIsSet)
{
    HostPrintln("[stub] NetDll___WSAFDIsSet(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_accept)
{
    HostPrintln("[stub] NetDll_accept(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_bind)
{
    HostPrintln("[stub] NetDll_bind(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_closesocket)
{
    HostPrintln("[stub] NetDll_closesocket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_connect)
{
    HostPrintln("[stub] NetDll_connect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_getsockname)
{
    HostPrintln("[stub] NetDll_getsockname(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_inet_addr)
{
    HostPrintln("[stub] NetDll_inet_addr(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_ioctlsocket)
{
    HostPrintln("[stub] NetDll_ioctlsocket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_listen)
{
    HostPrintln("[stub] NetDll_listen(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_recvfrom)
{
    HostPrintln("[stub] NetDll_recvfrom(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_select)
{
    HostPrintln("[stub] NetDll_select(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_send)
{
    HostPrintln("[stub] NetDll_send(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_sendto)
{
    HostPrintln("[stub] NetDll_sendto(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_setsockopt)
{
    HostPrintln("[stub] NetDll_setsockopt(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_shutdown)
{
    HostPrintln("[stub] NetDll_shutdown(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_socket)
{
    HostPrintln("[stub] NetDll_socket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSACleanup)
{
    HostPrintln("[stub] NetDll_WSACleanup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSAGetLastError)
{
    HostPrintln("[stub] NetDll_WSAGetLastError(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_WSAStartup)
{
    HostPrintln("[stub] NetDll_WSAStartup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetCleanup)
{
    HostPrintln("[stub] NetDll_XNetCleanup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetConnect)
{
    HostPrintln("[stub] NetDll_XNetConnect(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetGetConnectStatus)
{
    HostPrintln("[stub] NetDll_XNetGetConnectStatus(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetGetEthernetLinkStatus)
{
    HostPrintln("[stub] NetDll_XNetGetEthernetLinkStatus(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetGetTitleXnAddr)
{
    // Real signature confirmed against Xenia's NetDll_XNetGetTitleXnAddr_entry
    // (src/xenia/kernel/xam/xam_net.cc:452-475): (caller, XNADDR* addr_ptr) --
    // r3=caller, r4=addr_ptr. Real return values are status flags, not a bool;
    // critically XNET_GET_XNADDR_PENDING == 0, the same value this stub always
    // returned -- meaning every prior call reported "still resolving," so any
    // caller polling in a loop until it stops being pending would spin forever.
    // Confirmed live: exactly this busy-loop, thousands of identical calls in a
    // row (phase3 spec, Finding 47).
    //
    // Fixed to match Xenia's own real behavior: populate the XNADDR structure
    // with the same loopback/placeholder values (real XDK struct layout: ina
    // u32 @0, inaOnline u32 @4, wPortOnline u16 @8, abEnet[6] @10, abOnline[20]
    // @16, size 36) and return XNET_GET_XNADDR_STATIC (0x4) -- a settled,
    // non-pending status, matching a real offline-but-networked console
    // (appropriate for this project's single-player-backup scope, no real
    // Xbox Live connectivity implemented).
    HostPrintln("[stub] NetDll_XNetGetTitleXnAddr(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);

    uint32_t addrPtr = (uint32_t)ctx.r4.u64;
    if (addrPtr != 0)
    {
        constexpr uint32_t kInAddrLoopback = 0x7F000001; // 127.0.0.1
        PPC_STORE_U32(addrPtr + 0, kInAddrLoopback);      // ina
        PPC_STORE_U32(addrPtr + 4, 0);                    // inaOnline
        PPC_STORE_U16(addrPtr + 8, 0);                    // wPortOnline
        for (uint32_t i = 0; i < 6; i++)
        {
            PPC_STORE_U8(addrPtr + 10 + i, 0xCC);          // abEnet, placeholder MAC
        }
        for (uint32_t i = 0; i < 20; i++)
        {
            PPC_STORE_U8(addrPtr + 16 + i, 0);             // abOnline
        }
    }

    constexpr uint32_t kXNetGetXnAddrStatic = 0x00000004;
    ctx.r3.u64 = kXNetGetXnAddrStatic;
}

PPC_FUNC(__imp__NetDll_XNetQosListen)
{
    HostPrintln("[stub] NetDll_XNetQosListen(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetQosLookup)
{
    HostPrintln("[stub] NetDll_XNetQosLookup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetQosRelease)
{
    HostPrintln("[stub] NetDll_XNetQosRelease(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetStartup)
{
    HostPrintln("[stub] NetDll_XNetStartup(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetXnAddrToInAddr)
{
    HostPrintln("[stub] NetDll_XNetXnAddrToInAddr(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtCancelTimer)
{
    HostPrintln("[stub] NtCancelTimer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtClearEvent)
{
    HostPrintln("[stub] NtClearEvent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtCreateTimer)
{
    HostPrintln("[stub] NtCreateTimer(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtDuplicateObject)
{
    HostPrintln("[stub] NtDuplicateObject(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtFlushBuffersFile)
{
    HostPrintln("[stub] NtFlushBuffersFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtFreeVirtualMemory)
{
    HostPrintln("[stub] NtFreeVirtualMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryDirectoryFile)
{
    HostPrintln("[stub] NtQueryDirectoryFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryFullAttributesFile)
{
    HostPrintln("[stub] NtQueryFullAttributesFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryVirtualMemory)
{
    HostPrintln("[stub] NtQueryVirtualMemory(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtQueryVolumeInformationFile)
{
    HostPrintln("[stub] NtQueryVolumeInformationFile(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtResumeThread)
{
    HostPrintln("[stub] NtResumeThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtSetTimerEx)
{
    HostPrintln("[stub] NtSetTimerEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NtSuspendThread)
{
    HostPrintln("[stub] NtSuspendThread(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ObCreateSymbolicLink)
{
    HostPrintln("[stub] ObCreateSymbolicLink(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__ObDeleteSymbolicLink)
{
    HostPrintln("[stub] ObDeleteSymbolicLink(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlCompareMemoryUlong)
{
    HostPrintln("[stub] RtlCompareMemoryUlong(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlImageXexHeaderField)
{
    HostPrintln("[stub] RtlImageXexHeaderField(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlMultiByteToUnicodeN)
{
    HostPrintln("[stub] RtlMultiByteToUnicodeN(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlRaiseException)
{
    HostPrintln("[stub] RtlRaiseException(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlTimeFieldsToTime)
{
    HostPrintln("[stub] RtlTimeFieldsToTime(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlTryEnterCriticalSection)
{
    HostPrintln("[stub] RtlTryEnterCriticalSection(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlUnicodeToMultiByteN)
{
    HostPrintln("[stub] RtlUnicodeToMultiByteN(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__RtlUnwind)
{
    HostPrintln("[stub] RtlUnwind(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__StfsControlDevice)
{
    HostPrintln("[stub] StfsControlDevice(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__StfsCreateDevice)
{
    HostPrintln("[stub] StfsCreateDevice(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdEnableDisableClockGating)
{
    HostPrintln("[stub] VdEnableDisableClockGating(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__VdQueryVideoFlags)
{
    HostPrintln("[stub] VdQueryVideoFlags(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamAlloc)
{
    HostPrintln("[stub] XamAlloc(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentClose)
{
    HostPrintln("[stub] XamContentClose(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentCreateEx)
{
    HostPrintln("[stub] XamContentCreateEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentGetDeviceData)
{
    HostPrintln("[stub] XamContentGetDeviceData(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamContentSetThumbnail)
{
    HostPrintln("[stub] XamContentSetThumbnail(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamEnumerate)
{
    HostPrintln("[stub] XamEnumerate(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamFree)
{
    HostPrintln("[stub] XamFree(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamGetExecutionId)
{
    HostPrintln("[stub] XamGetExecutionId(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamGetSystemVersion)
{
    HostPrintln("[stub] XamGetSystemVersion(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamInputSetState)
{
    HostPrintln("[stub] XamInputSetState(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderGetLaunchData)
{
    HostPrintln("[stub] XamLoaderGetLaunchData(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderGetLaunchDataSize)
{
    HostPrintln("[stub] XamLoaderGetLaunchDataSize(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderLaunchTitle)
{
    HostPrintln("[stub] XamLoaderLaunchTitle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderSetLaunchData)
{
    HostPrintln("[stub] XamLoaderSetLaunchData(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamLoaderTerminateTitle)
{
    HostPrintln("[stub] XamLoaderTerminateTitle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamSessionCreateHandle)
{
    HostPrintln("[stub] XamSessionCreateHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamSessionRefObjByHandle)
{
    HostPrintln("[stub] XamSessionRefObjByHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowAchievementsUI)
{
    HostPrintln("[stub] XamShowAchievementsUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowDeviceSelectorUI)
{
    HostPrintln("[stub] XamShowDeviceSelectorUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowFriendsUI)
{
    HostPrintln("[stub] XamShowFriendsUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowGamerCardUIForXUID)
{
    HostPrintln("[stub] XamShowGamerCardUIForXUID(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowMessageBoxUIEx)
{
    HostPrintln("[stub] XamShowMessageBoxUIEx(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowPlayerReviewUI)
{
    HostPrintln("[stub] XamShowPlayerReviewUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamShowSigninUI)
{
    HostPrintln("[stub] XamShowSigninUI(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamTaskCloseHandle)
{
    HostPrintln("[stub] XamTaskCloseHandle(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamTaskSchedule)
{
    HostPrintln("[stub] XamTaskSchedule(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamTaskShouldExit)
{
    HostPrintln("[stub] XamTaskShouldExit(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserAreUsersFriends)
{
    HostPrintln("[stub] XamUserAreUsersFriends(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserCheckPrivilege)
{
    HostPrintln("[stub] XamUserCheckPrivilege(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserCreateStatsEnumerator)
{
    HostPrintln("[stub] XamUserCreateStatsEnumerator(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetName)
{
    HostPrintln("[stub] XamUserGetName(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetSigninState)
{
    HostPrintln("[stub] XamUserGetSigninState(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserGetXUID)
{
    HostPrintln("[stub] XamUserGetXUID(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserReadProfileSettings)
{
    HostPrintln("[stub] XamUserReadProfileSettings(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamUserWriteProfileSettings)
{
    HostPrintln("[stub] XamUserWriteProfileSettings(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceClose)
{
    HostPrintln("[stub] XamVoiceClose(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceCreate)
{
    HostPrintln("[stub] XamVoiceCreate(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceHeadsetPresent)
{
    HostPrintln("[stub] XamVoiceHeadsetPresent(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XamVoiceSubmitPacket)
{
    HostPrintln("[stub] XamVoiceSubmitPacket(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioGetVoiceCategoryVolume)
{
    HostPrintln("[stub] XAudioGetVoiceCategoryVolume(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioGetVoiceCategoryVolumeChangeMask)
{
    HostPrintln("[stub] XAudioGetVoiceCategoryVolumeChangeMask(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioSubmitRenderDriverFrame)
{
    HostPrintln("[stub] XAudioSubmitRenderDriverFrame(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XAudioUnregisterRenderDriverClient)
{
    HostPrintln("[stub] XAudioUnregisterRenderDriverClient(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XeCryptSha)
{
    HostPrintln("[stub] XeCryptSha(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XeKeysConsolePrivateKeySign)
{
    HostPrintln("[stub] XeKeysConsolePrivateKeySign(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XeKeysConsoleSignatureVerification)
{
    HostPrintln("[stub] XeKeysConsoleSignatureVerification(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XexGetProcedureAddress)
{
    HostPrintln("[stub] XexGetProcedureAddress(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetGameRegion)
{
    HostPrintln("[stub] XGetGameRegion(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetLanguage)
{
    HostPrintln("[stub] XGetLanguage(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMACreateContext)
{
    HostPrintln("[stub] XMACreateContext(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMAReleaseContext)
{
    HostPrintln("[stub] XMAReleaseContext(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgCancelIORequest)
{
    HostPrintln("[stub] XMsgCancelIORequest(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgInProcessCall)
{
    HostPrintln("[stub] XMsgInProcessCall(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XMsgStartIORequest)
{
    HostPrintln("[stub] XMsgStartIORequest(r3=0x{:X}, r4=0x{:X}, r5=0x{:X}, r6=0x{:X})", ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64);
    ctx.r3.u64 = 0;
}

