#include "pcan_loader.h"

static HMODULE g_hPcanModule = NULL;
static DWORD g_loadError = 0;

PFN_PCAN_Initialize Pcan_Initialize;
PFN_PCAN_Uninitialize Pcan_Uninitialize;
PFN_PCAN_Read Pcan_Read;
PFN_PCAN_Write Pcan_Write;
PFN_PCAN_FilterMessages Pcan_FilterMessages;
PFN_PCAN_LookUpChannel Pcan_LookUpChannel;
PFN_PCAN_GetErrorText Pcan_GetErrorText;

/* 单张表驱动加载、校验与清零，避免宏与指针数组两处重复维护 */
static struct {
    const char* name;
    void** ptr;
} g_procs[] = {
    { "CAN_Initialize",    (void**)&Pcan_Initialize },
    { "CAN_Uninitialize",  (void**)&Pcan_Uninitialize },
    { "CAN_Read",          (void**)&Pcan_Read },
    { "CAN_Write",         (void**)&Pcan_Write },
    { "CAN_FilterMessages",(void**)&Pcan_FilterMessages },
    { "CAN_LookUpChannel", (void**)&Pcan_LookUpChannel },
    { "CAN_GetErrorText",  (void**)&Pcan_GetErrorText },
};
#define PROC_COUNT (sizeof(g_procs) / sizeof(g_procs[0]))

int PcanLoader_IsLoaded(void) {
    return g_hPcanModule != NULL;
}

DWORD PcanLoader_GetLoadError(void) {
    return g_loadError;
}

int PcanLoader_Load(void) {
    if (g_hPcanModule) return 1;

    g_loadError = 0;
    g_hPcanModule = LoadLibraryW(L"PCANBasic.dll");
    if (!g_hPcanModule) {
        g_loadError = GetLastError();
        char buf[128];
        sprintf(buf, "加载 PCANBasic.dll 失败 (错误码: %lu)", g_loadError);
        OutputDebugStringA(buf);
        return 0;
    }

    for (int i = 0; i < (int)PROC_COUNT; i++)
        *g_procs[i].ptr = GetProcAddress(g_hPcanModule, g_procs[i].name);

    for (int i = 0; i < (int)PROC_COUNT; i++) {
        if (!*g_procs[i].ptr) {
            PcanLoader_Unload();
            return 0;
        }
    }
    return 1;
}

void PcanLoader_Unload(void) {
    if (g_hPcanModule) {
        FreeLibrary(g_hPcanModule);
        g_hPcanModule = NULL;
    }
    for (int i = 0; i < (int)PROC_COUNT; i++)
        *g_procs[i].ptr = NULL;
}
