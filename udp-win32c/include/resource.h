#ifndef RESOURCE_H
#define RESOURCE_H

/* 图标资源 */
#define IDI_APPICON                     101
#define IDI_APPICON_SMALL               102

/* 主对话框 */
#define IDD_MAIN                        201

/* 菜单 */
#define IDR_MAINMENU                    202
#define IDM_FILE_OPEN                   203
#define IDM_FILE_EXIT                   204
#define IDM_EDIT_CLEARLOG               205
#define IDM_HELP_ABOUT                  206

/* 控件 */
#define IDC_EDIT_IP                     301   /* 目标 IP (空=广播自动发现) */
#define IDC_EDIT_LOCAL_PORT             302   /* 本地监听端口 (默认 9201) */
#define IDC_EDIT_REMOTE_PORT            317   /* 远程端口 (默认 9200, 可设置) */
#define IDC_BUTTON_CONNECT              303
#define IDC_EDIT_FIRMWARE               304
#define IDC_BUTTON_BROWSE               305
#define IDC_CHECK_TESTMODE              306
#define IDC_PROGRESS                    307
#define IDC_BUTTON_FLASH                308
#define IDC_LABEL_VERSION               309
#define IDC_BUTTON_GETVERSION           310
#define IDC_BUTTON_REBOOT               311
#define IDC_EDIT_LOG                    312
#define IDC_BUTTON_CLEAR_LOG            313
#define IDC_LABEL_PERCENT               314

/* 应用常量 */
#define IDC_STATIC                      -1

#endif /* RESOURCE_H */
