
#if defined(UEFI_SOURCE) || defined(EFIAPI)
# include <Uefi.h>
# include <Library/UefiRuntimeServicesTableLib.h>
#else
# if defined(WINDOWS) || defined(_WINDOWS) || defined(_WIN32) || defined(_WIN64)
#  if defined(_M_AMD64) && !defined(_AMD64_)
#   define _AMD64_
#  endif
#  include <windef.h>
# endif
#endif

#include <stdint.h>

#include "LocalAppConfig.h"
#include "PciConfig.h"
#include "NvStrapsConfig.h"
#include "EfiVariable.h"
#include "StatusVar.h"

char const StatusVar_Name[MAX_VARIABLE_NAME_LENGTH] = "NvStrapsReBarStatus";

#if defined(UEFI_SOURCE) || defined(EFIAPI)
static uint_least64_t statusVar[NvStraps_GPU_MAX_COUNT + 1u] = { StatusVar_NotLoaded, StatusVar_NotLoaded };

static inline uint_least16_t MakeBusLocation(uint_least8_t bus, uint_least8_t device, uint_least8_t function)
{
    return (uint_least16_t)bus << 8u | (((uint_least16_t)device & 0b0001'1111u) << 3u) | (function & 0b0111u);
}

static inline UINT16 PciAddressToBusLocation(UINTN pciAddress)
{
    return MakeBusLocation(pciAddress >> 24u & 0xFFu, pciAddress >> 16u & 0xFFu, pciAddress >> 8u & 0xFFu);
}

static EFI_STATUS WriteStatusVar(uint_least16_t pciLocation)
{
    uint_least64_t var =
           ((uint_least64_t)pciLocation << (WORD_BITSIZE + DWORD_BITSIZE))
         | ((uint_least64_t)statusVar[0u] & UINT64_C(0x0000FFFF'FFFFFFFF));

    BYTE buffer[QWORD_SIZE];

    return WriteEfiVariable(StatusVar_Name, buffer, (uint_least32_t)(pack_QWORD(buffer, var) - buffer), EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS);
};

static void SetStatusVarInternal(StatusVar val, uint_least16_t pciLocation)
{
    if (val > statusVar[0u])
    {
        statusVar[0u] = val;
        WriteStatusVar(pciLocation);
    }
}

void SetStatusVar(StatusVar val)
{
    SetStatusVarInternal(val, 0u);
}

void SetEFIErrorInternal(EFIErrorLocation errLocation, EFI_STATUS status, uint_least16_t pciLocation)
{
    if (statusVar[0u] < UINT64_C(1) << DWORD_BITSIZE)
    {
        uint_least64_t value =
                   (uint_least64_t)(+errLocation & +BYTE_BITMASK) << (DWORD_BITSIZE + BYTE_BITSIZE)
                 | (uint_least64_t)(status & BYTE_BITMASK) << DWORD_BITSIZE
                 | StatusVar_Internal_EFIError;

        statusVar[0u] = value;
        WriteStatusVar(pciLocation);
    }
}

void SetEFIError(EFIErrorLocation errLocation, EFI_STATUS status)
{
    SetEFIErrorInternal(errLocation, status, 0u);
}

void SetDeviceEFIError(UINTN pciAddress, EFIErrorLocation errLocation, EFI_STATUS status)
{
    uint_least8_t bus, dev, fun;

    pciUnpackAddress(pciAddress, &bus, &dev, &fun);
    SetEFIErrorInternal(errLocation, status, pciPackLocation(bus, dev, fun));
}

void SetDeviceStatusVar(UINTN pciAddress, StatusVar val)
{
    uint_least8_t bus, dev, fun;

    pciUnpackAddress(pciAddress, &bus, &dev, &fun);
    SetStatusVarInternal(val, pciPackLocation(bus, dev, fun));
}
#else
uint_least64_t ReadStatusVar(ERROR_CODE *errorCode)
{
    BYTE buffer[QWORD_SIZE];
    uint_least32_t size = sizeof buffer;
    *errorCode = ReadEfiVariable(StatusVar_Name, buffer, &size);

    if (*errorCode)
        return StatusVar_NVAR_API_Error;

    if (size == 0u)
        return StatusVar_NotLoaded;

    if (size != sizeof buffer)
        return StatusVar_ParseError;

    return unpack_QWORD(buffer);
}
#endif
