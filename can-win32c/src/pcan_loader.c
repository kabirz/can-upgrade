#include "pcan_loader.h"

static HMODULE g_hPcanModule = NULL;

PFN_PCAN_Initialize Pcan_Initialize = NULL;
PFN_PCAN_Uninitialize Pcan_Uninitialize = NULL;
PFN_PCAN_Read Pcan_Read = NULL;
PFN_PCAN_Write Pcan_Write = NULL;
PFN_PCAN_FilterMessages Pcan_FilterMessages = NULL;
PFN_PCAN_LookUpChannel Pcan_LookUpChannel = NULL;
PFN_PCAN_GetErrorText Pcan_GetErrorText = NULL;

static void ClearPointers(void) {
    Pcan_Initialize = NULL;
    Pcan_Uninitialize = NULL;
    Pcan_Read = NULL;
    Pcan_Write = NULL;
    Pcan_FilterMessages = NULL;
    Pcan_LookUpChannel = NULL;
    Pcan_GetErrorText = NULL;
}

int PcanLoader_Load(void) {
    if (g_hPcanModule) return 1;

    g_hPcanModule = LoadLibraryW(L"PCANBasic.dll");
    if (!g_hPcanModule) return 0;

    Pcan_Initialize = (PFN_PCAN_Initialize)GetProcAddress(g_hPcanModule, "CAN_Initialize");
    Pcan_Uninitialize = (PFN_PCAN_Uninitialize)GetProcAddress(g_hPcanModule, "CAN_Uninitialize");
    Pcan_Read = (PFN_PCAN_Read)GetProcAddress(g_hPcanModule, "CAN_Read");
    Pcan_Write = (PFN_PCAN_Write)GetProcAddress(g_hPcanModule, "CAN_Write");
    Pcan_FilterMessages = (PFN_PCAN_FilterMessages)GetProcAddress(g_hPcanModule, "CAN_FilterMessages");
    Pcan_LookUpChannel = (PFN_PCAN_LookUpChannel)GetProcAddress(g_hPcanModule, "CAN_LookUpChannel");
    Pcan_GetErrorText = (PFN_PCAN_GetErrorText)GetProcAddress(g_hPcanModule, "CAN_GetErrorText");

    if (!Pcan_Initialize || !Pcan_Uninitialize || !Pcan_Read || !Pcan_Write ||
        !Pcan_FilterMessages || !Pcan_LookUpChannel || !Pcan_GetErrorText) {
        PcanLoader_Unload();
        return 0;
    }

    return 1;
}

void PcanLoader_Unload(void) {
    if (g_hPcanModule) {
        FreeLibrary(g_hPcanModule);
        g_hPcanModule = NULL;
    }
    ClearPointers();
}
