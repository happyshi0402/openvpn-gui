/*
 *  OpenVPN-GUI -- A Windows GUI for OpenVPN.
 *
 *  Copyright (C) 2004 Mathias Sundman <mathias@nilings.se>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _WIN32_IE 0x0500
#include <windows.h>
#include <shlwapi.h>
#include <Pbt.h>

#include "config.h"
#include "tray.h"
#include "openvpn.h"
#include "openvpn_config.h"
#include "viewlog.h"
#include "service.h"
#include "main.h"
#include "options.h"
#include "passphrase.h"
#include "proxy.h"
#include "registry.h"
#include "openvpn-gui-res.h"
#include "localization.h"

#ifndef DISABLE_CHANGE_PASSWORD
#include <openssl/evp.h>
#include <openssl/err.h>
#endif

/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK AboutDialogFunc (HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static void ShowSettingsDialog();
void CloseApplication(HWND hwnd);

/*  Class name and window title  */
TCHAR szClassName[ ] = _T("OpenVPN-GUI");
TCHAR szTitleText[ ] = _T("OpenVPN");

/* Options structure */
options_t o;

int WINAPI WinMain (HINSTANCE hThisInstance,
                    UNUSED HINSTANCE hPrevInstance,
                    UNUSED LPSTR lpszArgument,
                    UNUSED int nCmdShow)
{
  HWND hwnd;               /* This is the handle for our window */
  MSG messages;            /* Here messages to the application are saved */
  WNDCLASSEX wincl;        /* Data structure for the windowclass */
  DWORD shell32_version;


  /* initialize options to default state */
  InitOptions(&o);

#ifdef DEBUG
  /* Open debug file for output */
  if (!(o.debug_fp = fopen(DEBUG_FILE, "w")))
    {
      /* can't open debug file */
      ShowLocalizedMsg(IDS_ERR_OPEN_DEBUG_FILE, DEBUG_FILE);
      exit(1);
    }
  PrintDebug("Starting OpenVPN GUI v%s", PACKAGE_VERSION);
#endif


  o.hInstance = hThisInstance;

  if(!GetModuleHandle(_T("RICHED20.DLL")))
    {
      LoadLibrary(_T("RICHED20.DLL"));
    }
  else
    {
      /* can't load riched20.dll */
      ShowLocalizedMsg(IDS_ERR_LOAD_RICHED20);
      exit(1);
    }

  /* Check version of shell32.dll */
  shell32_version=GetDllVersion(_T("shell32.dll"));
  if (shell32_version < PACKVERSION(5,0))
    {
      /* shell32.dll version to low */
      ShowLocalizedMsg(IDS_ERR_SHELL_DLL_VERSION, shell32_version);
      exit(1);
    }
#ifdef DEBUG
  PrintDebug("Shell32.dll version: 0x%lx", shell32_version);
#endif


  /* Parse command-line options */
  ProcessCommandLine(&o, GetCommandLine());

  /* Check if a previous instance is already running. */
  if ((FindWindow (szClassName, NULL)) != NULL)
    {
        /* GUI already running */
        ShowLocalizedMsg(IDS_ERR_GUI_ALREADY_RUNNING);
        exit(1);
    }

  if (!GetRegistryKeys()) {
    exit(1);
  }
  if (!CheckVersion()) {
    exit(1);
  }
  BuildFileList();
  if (!VerifyAutoConnections()) {
    exit(1);
  }
  GetProxyRegistrySettings();

#ifndef DISABLE_CHANGE_PASSWORD
  /* Initialize OpenSSL */
  OpenSSL_add_all_algorithms();
  ERR_load_crypto_strings();
#endif

  /* The Window structure */
  wincl.hInstance = hThisInstance;
  wincl.lpszClassName = szClassName;
  wincl.lpfnWndProc = WindowProcedure;      /* This function is called by windows */
  wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
  wincl.cbSize = sizeof (WNDCLASSEX);

  /* Use default icon and mouse-pointer */
  wincl.hIcon = LoadLocalizedIcon(ID_ICO_APP);
  wincl.hIconSm = LoadLocalizedIcon(ID_ICO_APP);
  wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
  wincl.lpszMenuName = NULL;                 /* No menu */
  wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
  wincl.cbWndExtra = 0;                      /* structure or the window instance */
  /* Use Windows's default color as the background of the window */
  wincl.hbrBackground = (HBRUSH) COLOR_3DSHADOW; //COLOR_BACKGROUND;

  /* Register the window class, and if it fails quit the program */
  if (!RegisterClassEx (&wincl))
    return 1;

  /* The class is registered, let's create the program*/
  hwnd = CreateWindowEx (
           0,                   /* Extended possibilites for variation */
           szClassName,         /* Classname */
           szTitleText,         /* Title Text */
           WS_OVERLAPPEDWINDOW, /* default window */
           (int)CW_USEDEFAULT,  /* Windows decides the position */
           (int)CW_USEDEFAULT,  /* where the window ends up on the screen */
           230,                 /* The programs width */
           200,                 /* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           NULL,                /* No menu */
           hThisInstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );


  /* Run the message loop. It will run until GetMessage() returns 0 */
  while (GetMessage (&messages, NULL, 0, 0))
  {
    TranslateMessage(&messages);
    DispatchMessage(&messages);
  }

  /* The program return-value is 0 - The value that PostQuitMessage() gave */
  return messages.wParam;
}


/*  This function is called by the Windows function DispatchMessage()  */
LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  static UINT s_uTaskbarRestart;
  int i;

  switch (message) {
    case WM_CREATE:       

      /* Save Window Handle */
      o.hWnd = hwnd;

      s_uTaskbarRestart = RegisterWindowMessage(TEXT("TaskbarCreated"));

      CreatePopupMenus();	/* Create popup menus */  
      LoadAppIcon();		/* Load App Icon */
      ShowTrayIcon();
      if (o.allow_service[0]=='1' || o.service_only[0]=='1')
        CheckServiceStatus();	// Check if service is running or not
      if (!AutoStartConnections()) {
        SendMessage(hwnd, WM_CLOSE, 0, 0);
        break;
      }
      break;
    	
    case WM_NOTIFYICONTRAY:
      OnNotifyTray(lParam); 	// Manages message from tray
      return TRUE;
                 
    case WM_COMMAND:
      if ( (LOWORD(wParam) >= IDM_CONNECTMENU) && (LOWORD(wParam) < IDM_CONNECTMENU + MAX_CONFIGS) ) {
        StartOpenVPN(LOWORD(wParam) - IDM_CONNECTMENU);
      }
      if ( (LOWORD(wParam) >= IDM_DISCONNECTMENU) && (LOWORD(wParam) < IDM_DISCONNECTMENU + MAX_CONFIGS) ) {
        StopOpenVPN(LOWORD(wParam) - IDM_DISCONNECTMENU);
      }
      if ( (LOWORD(wParam) >= IDM_STATUSMENU) && (LOWORD(wParam) < IDM_STATUSMENU + MAX_CONFIGS) ) {
        ShowWindow(o.conn[LOWORD(wParam) - IDM_STATUSMENU].hwndStatus, SW_SHOW);
      }
      if ( (LOWORD(wParam) >= IDM_VIEWLOGMENU) && (LOWORD(wParam) < IDM_VIEWLOGMENU + MAX_CONFIGS) ) {
        ViewLog(LOWORD(wParam) - IDM_VIEWLOGMENU);
      }
      if ( (LOWORD(wParam) >= IDM_EDITMENU) && (LOWORD(wParam) < IDM_EDITMENU + MAX_CONFIGS) ) {
        EditConfig(LOWORD(wParam) - IDM_EDITMENU);
      }
#ifndef DISABLE_CHANGE_PASSWORD
      if ( (LOWORD(wParam) >= IDM_PASSPHRASEMENU) && (LOWORD(wParam) < IDM_PASSPHRASEMENU + MAX_CONFIGS) ) {
        ShowChangePassphraseDialog(LOWORD(wParam) - IDM_PASSPHRASEMENU);
      }
#endif
      if (LOWORD(wParam) == IDM_SETTINGS) {
        ShowSettingsDialog();
      }
      if (LOWORD(wParam) == IDM_ABOUT) {
        LocalizedDialogBox(ID_DLG_ABOUT, AboutDialogFunc);
      }
      if (LOWORD(wParam) == IDM_CLOSE) {
        CloseApplication(hwnd);
      }
      if (LOWORD(wParam) == IDM_SERVICE_START) {
        MyStartService();
      }
      if (LOWORD(wParam) == IDM_SERVICE_STOP) {
        MyStopService();
      }     
      if (LOWORD(wParam) == IDM_SERVICE_RESTART) MyReStartService();
      break;
	    
    case WM_CLOSE:
      CloseApplication(hwnd);
      break;

    case WM_DESTROY:
      StopAllOpenVPN();	
      OnDestroyTray();          /* Remove Tray Icon and destroy menus */
      PostQuitMessage (0);	/* Send a WM_QUIT to the message queue */
      break;

    case WM_QUERYENDSESSION:
      return(TRUE);

    case WM_ENDSESSION:
      StopAllOpenVPN();
      OnDestroyTray();
      break;

    case WM_POWERBROADCAST:
      switch (wParam) {
        case PBT_APMSUSPEND:
          if (o.disconnect_on_suspend[0] == '1')
            {
              /* Suspend running connections */
              for (i=0; i<o.num_configs; i++)
                {
                  if (o.conn[i].state == connected)
                SuspendOpenVPN(i);
                }

              /* Wait for all connections to suspend */
              for (i=0; i<10; i++, Sleep(500))
                if (CountConnState(suspending) == 0) break;
            }
          return FALSE;

        case PBT_APMRESUMESUSPEND:
        case PBT_APMRESUMECRITICAL:
          for (i=0; i<o.num_configs; i++)
            {
              /* Restart suspend connections */
              if (o.conn[i].state == suspended)
                StartOpenVPN(i);

              /* If some connection never reached SUSPENDED state */
              if (o.conn[i].state == suspending)
                StopOpenVPN(i);
            }
          return FALSE;
      }

    default:			/* for messages that we don't deal with */
      if (message == s_uTaskbarRestart)
        {
          /* Explorer has restarted, re-register the tray icon. */
          ShowTrayIcon();
          CheckAndSetTrayIcon();
          break;
        }      
      return DefWindowProc (hwnd, message, wParam, lParam);
  }

  return 0;
}


BOOL CALLBACK AboutDialogFunc (HWND hwndDlg, UINT msg, WPARAM wParam, UNUSED LPARAM lParam)
{
  HICON hIcon;

  switch (msg) {

    case WM_INITDIALOG:
      hIcon = LoadLocalizedIcon(ID_ICO_APP);
      if (hIcon) {
        SendMessage(hwndDlg, WM_SETICON, (WPARAM) (ICON_SMALL), (LPARAM) (hIcon));
        SendMessage(hwndDlg, WM_SETICON, (WPARAM) (ICON_BIG), (LPARAM) (hIcon));
      }
      break;

    case WM_COMMAND:
      switch (LOWORD(wParam)) {

        case IDOK:			// button
          EndDialog(hwndDlg, LOWORD(wParam));
          return TRUE;
      }
      break;

    case WM_CLOSE:
      EndDialog(hwndDlg, LOWORD(wParam));
      return TRUE;
     
  }
  return FALSE;
}


static void
ShowSettingsDialog()
{
  PROPSHEETPAGE psp[2];
  psp[0].dwSize = sizeof(PROPSHEETPAGE);
  psp[0].dwFlags = PSP_DLGINDIRECT;
  psp[0].hInstance = o.hInstance;
  psp[0].pResource = LocalizedDialogResource(ID_DLG_PROXY);
  psp[0].pfnDlgProc = ProxySettingsDialogFunc;
  psp[0].lParam = 0;
  psp[0].pfnCallback = NULL;
  psp[1].dwSize = sizeof(PROPSHEETPAGE);
  psp[1].dwFlags = PSP_DLGINDIRECT;
  psp[1].hInstance = o.hInstance;
  psp[1].pResource = LocalizedDialogResource(ID_DLG_GENERAL);
  psp[1].pfnDlgProc = LanguageSettingsDlgProc;
  psp[1].lParam = 0;
  psp[1].pfnCallback = NULL;

  PROPSHEETHEADER psh;
  psh.dwSize = sizeof(PROPSHEETHEADER);
  psh.dwFlags = PSH_USEHICON | PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
  psh.hwndParent = o.hWnd;
  psh.hInstance = o.hInstance;
  psh.hIcon = LoadLocalizedIcon(ID_ICO_APP);
  psh.pszCaption = LoadLocalizedString(IDS_SETTINGS_CAPTION);
  psh.nPages = sizeof(psp) / sizeof(PROPSHEETPAGE);
  psh.nStartPage = 0;
  psh.ppsp = (LPCPROPSHEETPAGE) &psp;
  psh.pfnCallback = NULL;

  PropertySheet(&psh);
}


void CloseApplication(HWND hwnd)
{
  int i, ask_exit=0;

  if (o.service_state == service_connected)
    {
      if (MessageBox(NULL, LoadLocalizedString(IDS_NFO_SERVICE_ACTIVE_EXIT), _T("Exit OpenVPN"), MB_YESNO) == IDNO)
        {
          return;
        }
    }

  for (i=0; i < o.num_configs; i++) {
    if (o.conn[i].state != disconnected) {
      ask_exit=1;
      break;
    }
  }
  if (ask_exit) {
    /* aks for confirmation */
    if (MessageBox(NULL, LoadLocalizedString(IDS_NFO_ACTIVE_CONN_EXIT), _T("Exit OpenVPN"), MB_YESNO) == IDNO)
      {
        return;
      }
  }
  DestroyWindow(hwnd);  
}

#ifdef DEBUG
void PrintDebugMsg(char *msg)
{
  time_t log_time;
  struct tm *time_struct;
  char date[30];

  log_time = time(NULL);
  time_struct = localtime(&log_time);
  snprintf(date, sizeof(date), "%d-%.2d-%.2d %.2d:%.2d:%.2d",
                 time_struct->tm_year + 1900,
                 time_struct->tm_mon + 1,
                 time_struct->tm_mday,
                 time_struct->tm_hour,
                 time_struct->tm_min,
                 time_struct->tm_sec);

  fprintf(o.debug_fp, "%s %s\n", date, msg);
  fflush(o.debug_fp);
}

void PrintErrorDebug(char *msg)
{
  LPVOID lpMsgBuf;
  char *buf;

  /* Get last error message */
  if (!FormatMessage( 
          FORMAT_MESSAGE_ALLOCATE_BUFFER | 
          FORMAT_MESSAGE_FROM_SYSTEM | 
          FORMAT_MESSAGE_IGNORE_INSERTS,
          NULL,
          GetLastError(),
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
          (LPTSTR) &lpMsgBuf,
          0,
          NULL ))
    {
      /* FormatMessage failed! */
      PrintDebug("FormatMessage() failed. %s ", msg);
      return;
    }

  /* Cut of CR/LFs */
  buf = (char *)lpMsgBuf;
  buf[strlen(buf) - 3] = '\0';

  PrintDebug("%s %s", msg, (LPCTSTR)lpMsgBuf);

}
#endif

bool
init_security_attributes_allow_all (struct security_attributes *obj)
{
  CLEAR (*obj);

  obj->sa.nLength = sizeof (SECURITY_ATTRIBUTES);
  obj->sa.lpSecurityDescriptor = &obj->sd;
  obj->sa.bInheritHandle = FALSE;
  if (!InitializeSecurityDescriptor (&obj->sd, SECURITY_DESCRIPTOR_REVISION))
    return false;
  if (!SetSecurityDescriptorDacl (&obj->sd, TRUE, NULL, FALSE))
    return false;
  return true;
}

#define PACKVERSION(major,minor) MAKELONG(minor,major)
DWORD GetDllVersion(LPCTSTR lpszDllName)
{
    HINSTANCE hinstDll;
    DWORD dwVersion = 0;

    /* For security purposes, LoadLibrary should be provided with a 
       fully-qualified path to the DLL. The lpszDllName variable should be
       tested to ensure that it is a fully qualified path before it is used. */
    hinstDll = LoadLibrary(lpszDllName);
	
    if(hinstDll)
    {
        DLLGETVERSIONPROC pDllGetVersion;
        pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hinstDll, 
                          "DllGetVersion");

        /* Because some DLLs might not implement this function, you
        must test for it explicitly. Depending on the particular 
        DLL, the lack of a DllGetVersion function can be a useful
        indicator of the version. */

        if(pDllGetVersion)
        {
            DLLVERSIONINFO dvi;
            HRESULT hr;

            ZeroMemory(&dvi, sizeof(dvi));
            dvi.cbSize = sizeof(dvi);

            hr = (*pDllGetVersion)(&dvi);

            if(SUCCEEDED(hr))
            {
               dwVersion = PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion);
            }
        }

        FreeLibrary(hinstDll);
    }
    return dwVersion;
}
