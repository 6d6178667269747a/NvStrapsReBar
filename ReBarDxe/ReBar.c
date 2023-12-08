/*
Copyright (c) 2022-2023 xCuri0 <zkqri0@gmail.com>
SPDX-License-Identifier: MIT
*/
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci22.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

#include "include/pciRegs.h"
#include "include/PciHostBridgeResourceAllocation.h"
#include "include/SetupNvStraps.h"
#include "include/ReBar.h"

#ifdef _MSC_VER
#pragma warning(disable:28251)
#include <intrin.h>
#pragma warning(default:28251)
#endif
#include "Protocol/PciHostBridgeResourceAllocation.h"
// #include "Build/ReBarUEFI/RELEASE_VS2019/X64/ReBarUEFI/ReBarDxe/ReBarDxe/DEBUG/AutoGen.h"

#define PCI_POSSIBLE_ERROR(val) ((val) == 0xffffffff)

// for quirk
#define PCI_VENDOR_ID_ATI 0x1002

// if system time is before this year then CMOS reset will be detected and rebar will be disabled.
#define BUILD_YEAR 2023

// 481893f5-2436-4fd5-9d5a-69b121c3f0ba
static GUID reBarStateGuid = { 0x481893f5, 0x2436, 0x4fd5, {0x9d, 0x5a, 0x69, 0xb1, 0x21, 0xc3, 0xf0, 0xba}};

// 0: disabled
// >0: maximum BAR size (2^x) set to value. UINT8_MAX for unlimited
static UINT8 reBarState = 0;

static EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *pciResAlloc;
static EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *pciRootBridgeIo;

static EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL_PREPROCESS_CONTROLLER o_PreprocessController;

// find last set bit and return the index of it
INTN fls(UINT32 x)
{
    UINT32 r;

    #ifdef _MSC_VER
    _BitScanReverse64(&r, x);
    #else
    // taken from linux x86 bitops.h
    asm("bsrl %1,%0"
	    : "=r" (r)
	    : "rm" (x), "0" (-1));
    #endif

    return r;
}

UINT64 pciAddrOffset(UINTN pciAddress, INTN offset)
{
    UINTN reg = (pciAddress & 0xffffffff00000000) >> 32;
    UINTN bus = (pciAddress & 0xff000000) >> 24;
    UINTN dev = (pciAddress & 0xff0000) >> 16;
    UINTN func = (pciAddress & 0xff00) >> 8;

    return EFI_PCI_ADDRESS(bus, dev, func, ((INT64)reg + offset));
}

// created these functions to make it easy to read as we are adapting alot of code from Linux
EFI_STATUS pciReadConfigDword(UINTN pciAddress, INTN pos, UINT32 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigDword(UINTN pciAddress, INTN pos, UINT32 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}
EFI_STATUS pciReadConfigWord(UINTN pciAddress, INTN pos, UINT16 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigWord(UINTN pciAddress, INTN pos, UINT16 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciReadConfigByte(UINTN pciAddress, INTN pos, UINT8 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint8, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigByte(UINTN pciAddress, INTN pos, UINT8 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint8, pciAddrOffset(pciAddress, pos), 1, buf);
}

// adapted from linux pci_find_ext_capability
UINT16 pciFindExtCapability(UINTN pciAddress, INTN cap)
{
    INTN ttl;
    UINT32 header;
    UINT16 pos = PCI_CFG_SPACE_SIZE;

    /* minimum 8 bytes per capability */
    ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos, &header)))
        return 0;
    /*
     * If we have no capabilities, this is indicated by cap ID,
     * cap version and next pointer all being 0. Or it could also be all FF
     */
    if (header == 0 || PCI_POSSIBLE_ERROR(header))
        return 0;

    while (ttl-- > 0)
    {
        if (PCI_EXT_CAP_ID(header) == cap && pos != 0)
            return pos;

        pos = PCI_EXT_CAP_NEXT(header);
        if (pos < PCI_CFG_SPACE_SIZE)
            break;

        if (EFI_ERROR(pciReadConfigDword(pciAddress, pos, &header)))
            break;
    }
    return 0;
}

INTN pciRebarFindPos(UINTN pciAddress, INTN pos, UINT8 bar)
{
    UINTN nbars, i;
    UINT32 ctrl;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
            PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbars; i++, pos += 8)
    {
        UINTN bar_idx;

        pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
        bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
        if (bar_idx == bar)
            return pos;
    }
    
    return -1;
}

UINT32 pciRebarGetPossibleSizes(UINTN pciAddress, UINTN epos, UINT16 vid, UINT16 did, UINT8 bar)
{
    INTN pos;
    UINT32 cap;

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0)
        return 0;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CAP, &cap);
    cap &= PCI_REBAR_CAP_SIZES;

    /* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
    if (vid == PCI_VENDOR_ID_ATI && did == 0x731f &&
        bar == 0 && cap == 0x7000)
        cap = 0x3f000;

    if (NvStraps_CheckBARSizeListAdjust(pciAddress, vid, did, bar))
        cap = NvStraps_AdjustBARSizeList(pciAddress, vid, did, bar, cap);

    return cap >> 4;
}

INTN pciRebarSetSize(UINTN pciAddress, UINTN epos, UINT8 bar, UINT8 size)
{
    INTN pos;
    UINT32 ctrl;

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0)
        return pos;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    ctrl &= (UINT32)~PCI_REBAR_CTRL_BAR_SIZE;
    ctrl |= (UINT32)size << PCI_REBAR_CTRL_BAR_SHIFT;

    pciWriteConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    return 0;
}

VOID reBarSetupDevice(EFI_HANDLE handle, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS addrInfo)
{
    UINTN epos;
    UINT16 vid, did;
    UINTN pciAddress;

    gBS->HandleProtocol(handle, &gEfiPciRootBridgeIoProtocolGuid, (void **)&pciRootBridgeIo);

    pciAddress = EFI_PCI_ADDRESS(addrInfo.Bus, addrInfo.Device, addrInfo.Function, 0);
    pciReadConfigWord(pciAddress, 0, &vid);
    pciReadConfigWord(pciAddress, 2, &did);

    if (vid == 0xFFFF)
        return;

    DEBUG((DEBUG_INFO, "ReBarDXE: Device vid:%x did:%x\n", vid, did));

    NvStraps_EnumDevice(vid, did, &addrInfo, reBarState);

    if (NvStraps_CheckDevice(vid, did, &addrInfo))
        NvStraps_Setup(vid, did, &addrInfo);

    epos = pciFindExtCapability(pciAddress, PCI_EXT_CAP_ID_REBAR);
    if (epos)
    {
        for (UINT8 bar = 0u; bar < PCI_STD_NUM_BARS; bar++)
        {
            UINT32 rBarS = pciRebarGetPossibleSizes(pciAddress, epos, vid, did, bar);
            if (!rBarS)
                continue;
            // start with size from fls
            for (UINT8 n = MIN((UINT8)fls(rBarS), reBarState); n > 0; n--) {
                // check if size is supported
                if (rBarS & (1 << n)) {
                    pciRebarSetSize(pciAddress, epos, bar, n);
                    break;
                }
            }
        }
    }
}

EFI_STATUS EFIAPI PreprocessControllerOverride (
  IN  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL  *This,
  IN  EFI_HANDLE                                        RootBridgeHandle,
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS       PciAddress,
  IN  EFI_PCI_CONTROLLER_RESOURCE_ALLOCATION_PHASE      Phase
  )
{
    // call the original method
    EFI_STATUS status = o_PreprocessController(This, RootBridgeHandle, PciAddress, Phase);

    DEBUG((DEBUG_INFO, "ReBarDXE: Hooked PreprocessController called %d\n", Phase));

    // EDK2 PciBusDxe setups Resizable BAR twice so we will do same
    if (Phase <= EfiPciBeforeResourceCollection) {
        reBarSetupDevice(RootBridgeHandle, PciAddress);
    }

    return status;
}

VOID pciHostBridgeResourceAllocationProtocolHook()
{
    EFI_STATUS status;
    UINTN handleCount;
    EFI_HANDLE *handleBuffer;

    status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiPciHostBridgeResourceAllocationProtocolGuid,
        NULL,
        &handleCount,
        &handleBuffer);

    if (EFI_ERROR(status))
        goto free;

    status = gBS->OpenProtocol(
        handleBuffer[0],
        &gEfiPciHostBridgeResourceAllocationProtocolGuid,
        (VOID **)&pciResAlloc,
        gImageHandle,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);

    if (EFI_ERROR(status))
        goto free;

    DEBUG((DEBUG_INFO, "ReBarDXE: Hooking EfiPciHostBridgeResourceAllocationProtocol->PreprocessController\n"));

    // Hook PreprocessController
    o_PreprocessController = pciResAlloc->PreprocessController;
    pciResAlloc->PreprocessController = &PreprocessControllerOverride;

free:
    FreePool(handleBuffer);
}

EFI_STATUS EFIAPI rebarInit(
    IN EFI_HANDLE imageHandle,
    IN EFI_SYSTEM_TABLE *systemTable)
{
    UINTN bufferSize = 1;
    EFI_STATUS status;
    UINT32 attributes;
    EFI_TIME time;

    DEBUG((DEBUG_INFO, "ReBarDXE: Loaded\n"));

    // Read ReBarState variable
    status = gRT->GetVariable(L"NvStrapsReBar", &reBarStateGuid,
        &attributes,
        &bufferSize, &reBarState);

    // any attempts to overflow reBarState should result in EFI_BUFFER_TOO_SMALL
    if (status != EFI_SUCCESS)
        reBarState = 0;

    if (reBarState)
    {
        DEBUG((DEBUG_INFO, "ReBarDXE: Enabled, maximum BAR size 2^%u MB\n", reBarState));

        // Detect CMOS reset by checking if year before BUILD_YEAR
        status = gRT->GetTime (&time, NULL);
        if (time.Year < BUILD_YEAR) {
            reBarState = 0;
            bufferSize = 1;
            attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

            status = gRT->SetVariable(L"NvStrapsReBar", &reBarStateGuid,
                attributes,
                bufferSize, &reBarState);

            return EFI_SUCCESS;
        }

        // For overriding PciHostBridgeResourceAllocationProtocol
        pciHostBridgeResourceAllocationProtocolHook();
    }

    return EFI_SUCCESS;
}
