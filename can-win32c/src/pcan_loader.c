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

#define LOAD_PROC(name) \
    Pcan_##name = (PFN_PCAN_##name)GetProcAddress(g_hPcanModule, "CAN_" #name)

#define FUNC_PTR(name) (void**)&Pcan_##name

static void* const g_funcPtrs[] = {
    FUNC_PTR(Initialize), FUNC_PTR(Uninitialize), FUNC_PTR(Read),
    FUNC_PTR(Write), FUNC_PTR(FilterMessages), FUNC_PTR(LookUpChannel),
    FUNC_PTR(GetErrorText)
};

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

    LOAD_PROC(Initialize);
    LOAD_PROC(Uninitialize);
    LOAD_PROC(Read);
    LOAD_PROC(Write);
    LOAD_PROC(FilterMessages);
    LOAD_PROC(LookUpChannel);
    LOAD_PROC(GetErrorText);

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
    for (int i = 0; i < sizeof(g_funcPtrs)/sizeof(g_funcPtrs[0]); i++)
        *(void**)g_funcPtrs[i] = NULL;
}
