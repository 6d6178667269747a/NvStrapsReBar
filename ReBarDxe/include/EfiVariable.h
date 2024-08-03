#if !defined(NV_STRAPS_REBAR_EFI_VARIABLE_H)
#define NV_STRAPS_REBAR_EFI_VARIABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "LocalAppConfig.h"

#if defined(__cplusplus)
extern "C"
{
#endif

enum
{
   MAX_VARIABLE_NAME_LENGTH = 64u
};

#if defined(WINDOWS_SOURCE)
# if !defined(EFI_VARIABLE_NON_VOLATILE) && !defined(EFI_VARIABLE_BOOTSERVICE_ACCESS) && !defined(EFI_VARIABLE_RUNTIME_ACCESS)
enum
{
    EFI_VARIABLE_NON_VOLATILE = UINT32_C(0x0000'0001),
    EFI_VARIABLE_BOOTSERVICE_ACCESS = UINT32_C(0x0000'0002),
    EFI_VARIABLE_RUNTIME_ACCESS = UINT32_C(0x0000'0004),
    EFI_VARIABLE_HARDWARE_ERROR_RECORD = UINT32_C(0x0000'0008)
};
# endif
#endif

uint_least8_t unpack_BYTE(BYTE const *buffer);
uint_least16_t unpack_WORD(BYTE const *buffer);
uint_least32_t unpack_DWORD(BYTE const *buffer);
uint_least64_t unpack_QWORD(BYTE const *buffer);
BYTE *pack_BYTE(BYTE *buffer, uint_least8_t value);
BYTE *pack_WORD(BYTE *buffer, uint_least16_t value);
BYTE *pack_DWORD(BYTE *buffer, uint_least32_t value);
BYTE *pack_QWORD(BYTE *buffer, uint_least64_t value);

ERROR_CODE ReadEfiVariable(char const name[MAX_VARIABLE_NAME_LENGTH], BYTE *buffer, uint_least32_t *size);
ERROR_CODE WriteEfiVariable(char const name[MAX_VARIABLE_NAME_LENGTH], BYTE /* const */ *buffer, uint_least32_t size, uint_least32_t attributes);

#if defined(__cplusplus)
}       // extern "C"
#endif

#endif          // !defined(NV_STRAPS_REBAR_EFI_VARIABLE_H)
