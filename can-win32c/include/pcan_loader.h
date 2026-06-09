#ifndef PCAN_LOADER_H
#define PCAN_LOADER_H

#include <windows.h>
#include "PCANBasic.h"

#ifdef __cplusplus
extern "C" {
#endif

int PcanLoader_Load(void);
int PcanLoader_IsLoaded(void);
void PcanLoader_Unload(void);

typedef TPCANStatus (__stdcall *PFN_PCAN_Initialize)(TPCANHandle, TPCANBaudrate, TPCANType, DWORD, WORD);
typedef TPCANStatus (__stdcall *PFN_PCAN_Uninitialize)(TPCANHandle);
typedef TPCANStatus (__stdcall *PFN_PCAN_Read)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
typedef TPCANStatus (__stdcall *PFN_PCAN_Write)(TPCANHandle, TPCANMsg*);
typedef TPCANStatus (__stdcall *PFN_PCAN_FilterMessages)(TPCANHandle, DWORD, DWORD, TPCANMode);
typedef TPCANStatus (__stdcall *PFN_PCAN_LookUpChannel)(LPSTR, TPCANHandle*);
typedef TPCANStatus (__stdcall *PFN_PCAN_GetErrorText)(TPCANStatus, WORD, LPSTR);

extern PFN_PCAN_Initialize Pcan_Initialize;
extern PFN_PCAN_Uninitialize Pcan_Uninitialize;
extern PFN_PCAN_Read Pcan_Read;
extern PFN_PCAN_Write Pcan_Write;
extern PFN_PCAN_FilterMessages Pcan_FilterMessages;
extern PFN_PCAN_LookUpChannel Pcan_LookUpChannel;
extern PFN_PCAN_GetErrorText Pcan_GetErrorText;

#ifdef __cplusplus
}
#endif

#endif
