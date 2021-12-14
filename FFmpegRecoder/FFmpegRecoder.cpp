// FFmpegRecoder.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "FFmpegRecoder.h"

#include <mmsystem.h>
#include <commdlg.h>
#include <windowsx.h>

#define MAX_LOADSTRING	100
#define BUFFER_LENGTH	3000
#define BUSY_VIDEO_STREAM	"Busy_Vid_Stream"
#define BUSY_AUDIO_STREAM	"Busy_Aud_Stream"
#define	BUSY_WRITE_MEDIA	"Busy_Write_Media"

BYTE	pAudioAccBuffer[BUFFER_LENGTH];
byte	disp[BUFFER_LENGTH * 100 * 4];
BYTE	waterMark[4 * WATERMARK_WIDTH * WATERMARK_HEIGHT];
BYTE	tmpWatermark[4 * WATERMARK_WIDTH * WATERMARK_HEIGHT];

// Global Variables:
BOOL		bRecord = false;

int			nSelectedVID = -1;
int			nSelectedAUD = -1;

HANDLE		hMainThread = INVALID_HANDLE_VALUE;
HANDLE		hVideoThread = INVALID_HANDLE_VALUE;
HANDLE		hAudioThread = INVALID_HANDLE_VALUE;
HANDLE		hBusyVideo = INVALID_HANDLE_VALUE;
HANDLE		hBusyAudio = INVALID_HANDLE_VALUE;

UINT32		width = 0;
UINT32		height = 0;

BYTE		lpAudioBlock[INP_BUFFER_SIZE];
DWORD		dwAudioBlockLength = 0;

HINSTANCE	hInst;									// current instance
WCHAR		szTitle[MAX_LOADSTRING];                // The title bar text
WCHAR		szWindowClass[MAX_LOADSTRING];          // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

void logOnWindow(HWND hWnd, char *str, int val) {
	char buf[256];

	memset(buf, 0, 256);
	sprintf(buf, str, val);
	SetWindowTextA(hWnd, buf);
}

void Warn_HR(HWND hWnd, const TCHAR *msg, HRESULT hr) {
	TCHAR buf[256];
	_com_error err(hr);

	memset(buf, 0, 256);
	wsprintf(buf, L"%s%s", msg, err.ErrorMessage());
	MessageBox(hWnd, buf, L"Error", MB_OK);
}


void exitRelease() {
	bStopFlag = true;
	WaitForSingleObject(hMainThread, INFINITE);
	
	CloseHandle(hMainThread);
	CloseHandle(hAudioThread);
	CloseHandle(hVideoThread);

	CloseHandle(hBusy);
	CloseHandle(hBusyAudio);
	CloseHandle(hBusyVideo);
}

void getWorkDir() {
	char exe[MAX_PATH] = { 0 };
	char szDir[MAX_PATH] = { 0 };

	memset(szFile, 0, MAX_PATH);
	GetModuleFileNameA(NULL, exe, sizeof(exe));
	char *tmp = NULL;

	if (tmp = strrchr(exe, '\\'))
	{
		tmp[1] = 0;
	}
	strcpy(szDir, exe);
	sprintf(szFile, "%soutput.mp4", szDir);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_FFMPEGRECODER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);


    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FFMPEGRECODER));

	getWorkDir();
	atexit(exitRelease);

	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DLG_MAIN), NULL, (DLGPROC)WndProc);

	return 0;
}

void ShowImage(HWND hWnd, BYTE *buffer, UINT32 width, UINT32 height) {
	HDC hdc = GetDC(hWnd);
	HDC hdcMem = CreateCompatibleDC(hdc);
	HGDIOBJ oldBitmap;
	HBITMAP hBmp;
	RECT rect;
	GetClientRect(hWnd, &rect);

	hBmp = CreateBitmap(width, height, 1, 32, buffer);
	SetStretchBltMode(hdc, HALFTONE);

	oldBitmap = SelectObject(hdcMem, hBmp);
	int wid = rect.right - rect.left;
	int hig = rect.bottom - rect.top;

	StretchBlt(hdc, 0, 0, wid, hig, hdcMem, 0, 0, width, height, SRCCOPY);

	SelectObject(hdcMem, oldBitmap);
	DeleteDC(hdcMem);
	ReleaseDC(hWnd, hdc);
	DeleteDC(hdc);
	DeleteObject(hBmp);
}

void receiveVideo(void *threadArg) {
	long	tick = 0;
	int vidIndex = -1;
	HWND	hWnd = (HWND)threadArg;
	HRESULT	hr = S_OK;

	while (bStopFlag == false)
	{
		tick = GetTickCount();

		if (nSelectedVID != vidIndex) {
			if (vidIndex != -1) {
				vmm.releaseMediaSource();
			}

			vidIndex = nSelectedVID;

			hr = vmm.initMediaSourceByIndex(nSelectedVID, hWnd);

			if (FAILED(hr)) {
				vidIndex = -1;

				Warn_HR(hWnd, L"Unable to intialize video device\n", hr);
				continue;
			}
		}

		if (vidIndex != -1) {
			WaitForSingleObject(hBusy, INFINITE);
			ResetEvent(hBusyVideo);

			hr = vmm.readFrame();

			SetEvent(hBusyVideo);

			if (FAILED(hr)) continue;

			ShowImage(GetDlgItem(hWnd, IDC_PREVIEW), vmm.pRgbBuffer, width, height);
		}
	}
}

void receiveAudio(void *threadArg) {
	long	tick = 0;
	int		audIndex = -1;
	int		pos = 0;
	HRESULT	hr = S_OK;
	HWND	hWnd = (HWND)threadArg;

	memset(pAudioAccBuffer, 0, BUFFER_LENGTH);

	while (bStopFlag == false)
	{
		tick = GetTickCount();

		if (nSelectedAUD != audIndex) {
			if (audIndex != -1) {
				amm.releaseMediaSource();
			}

			audIndex = nSelectedAUD;

			hr = amm.initMediaSourceByIndex(nSelectedAUD, hWnd);

			if (FAILED(hr)) {
				bFoundRecDevice = false;
				bAudioEnabled = false;
				audIndex = -1;

				Warn_HR(hWnd, L"Unable to intialize audio device\n", hr);
				continue;
			}
			bFoundRecDevice = true;
			bAudioEnabled = true;
		}

		if (audIndex != -1) {
			WaitForSingleObject(hBusy, INFINITE);

			ResetEvent(hBusyAudio);
			
			if (bRecord == false) amm.dwbufLength = 0;

			hr = amm.readFrame();

			if (FAILED(hr)) continue;

			SetEvent(hBusyAudio);

			if (dwAudioBlockLength > 0) {
				memset(disp, 0, BUFFER_LENGTH * 100 * 4);

				if (pos + dwAudioBlockLength > BUFFER_LENGTH) {
					DWORD rest = BUFFER_LENGTH - dwAudioBlockLength;

					memcpy(pAudioAccBuffer, pAudioAccBuffer + dwAudioBlockLength, rest);
					pos = BUFFER_LENGTH - dwAudioBlockLength;
				}

				memcpy(pAudioAccBuffer + pos, lpAudioBlock, dwAudioBlockLength);
				
				pos += dwAudioBlockLength;

				for (int n = 0; n < BUFFER_LENGTH; n++) {
					float x = (float)((int)pAudioAccBuffer[n] - 128) * 500.0f / 128.0f;
					x += 50;
					if (x < 0) x = 0;
					if (x > 99) x = 99;
					int st = 0;
					int ed = 0;

					if (x < 50) {
						st = (int)x; ed = 50;
					}
					else {
						st = 50; ed = (int)x;
					}
					for (int i = st; i <= ed; i++) {
						disp[i * BUFFER_LENGTH * 4 + n * 4] = 255;
						disp[i * BUFFER_LENGTH * 4 + n * 4 + 1] = 0;
						disp[i * BUFFER_LENGTH * 4 + n * 4 + 2] = 0;
						disp[i * BUFFER_LENGTH * 4 + n * 4 + 3] = 255;
					}
				}
				ShowImage(GetDlgItem(hWnd, IDC_AUDIO_PREVIEW), disp, BUFFER_LENGTH, 100);
			}
		}

	}
}

void receiveFromMediaSources(void *threadArg) {
	HRESULT	hr = S_OK;
	long tick = 0;
	long lTick = 0;

	hr = MFStartup(MF_VERSION);

	while (bStopFlag == false)
	{
		tick = GetTickCount();

		if (nSelectedAUD == -1 || nSelectedVID == -1) continue;

		WaitForSingleObject(hBusyAudio, INFINITE);
		WaitForSingleObject(hBusyVideo, INFINITE);

		ResetEvent(hBusy);

		if(bRecord) {
			WriteMedia();
		}

		SetEvent(hBusy);

		tick = GetTickCount() - tick;
		
		tick = 40 - tick;
		if (tick < 0) tick = 1;
		//Sleep(tick);
	}

	if (bRecord) {
		CloseMedia();
		bRecord = false;
	}

	WaitForSingleObject(hAudioThread, INFINITE);
	WaitForSingleObject(hVideoThread, INFINITE);

	vmm.release();
	amm.release();

	MFShutdown();
}


//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FFMPEGRECODER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_FFMPEGRECODER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

LRESULT CALLBACK DrawDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static RECT rt;

	switch (message)
	{
	case WM_INITDIALOG:
		{
			GetClientRect(GetDlgItem(hWnd, IDC_DRAW_VIEW), &rt);
			memcpy(tmpWatermark, waterMark, 160000);
		}
		break;
	case WM_MOUSEMOVE:
	{
		char buf[256];
		int penWidth = 5;
		double sX = ((float)WATERMARK_WIDTH / (double)rt.right);
		double sY = ((float)WATERMARK_HEIGHT / (double)rt.bottom);

		int xPos = GET_X_LPARAM(lParam);
		int yPos = GET_Y_LPARAM(lParam);
		xPos -= 10; yPos -= 10;
		if (xPos < rt.left || xPos > rt.right) break;
		if (yPos < rt.top || yPos > rt.bottom) break;

		sprintf(buf, "%d-%d", xPos, yPos);
		SetWindowTextA(hWnd, buf);

		if (wParam != MK_LBUTTON) break;
		for (int y = yPos - penWidth; y <= yPos + penWidth; y++) {
			if (y < 0 || y >= rt.bottom) continue;

			int yy = (int)((double)y * sY);
			BYTE *ptr = tmpWatermark + yy * 4 * WATERMARK_WIDTH;
			for (int x = xPos - penWidth; x <= xPos + penWidth; x++) {
				if (x < 0 || x >= rt.right) continue;
				int xx = (int)((double)x * sX);
				ptr[xx * 4] = 0;
				ptr[xx * 4 + 1] = 0;
				ptr[xx * 4 + 2] = 255;
				ptr[xx * 4 + 3] = 255;
			}
		}

		ShowImage(GetDlgItem(hWnd, IDC_DRAW_VIEW), tmpWatermark, WATERMARK_WIDTH, WATERMARK_HEIGHT);
	}
	break;
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDC_WATERMARK_CLEAR:
			memset(waterMark, 0, 4 * WATERMARK_WIDTH * WATERMARK_HEIGHT);
			memset(tmpWatermark, 0, 4 * WATERMARK_WIDTH * WATERMARK_HEIGHT);
			ShowImage(GetDlgItem(hWnd, IDC_DRAW_VIEW), tmpWatermark, WATERMARK_WIDTH, WATERMARK_HEIGHT);
			break;
		case IDC_APPLY:
			memcpy(waterMark, tmpWatermark, 4 * WATERMARK_WIDTH * WATERMARK_HEIGHT);
			EndDialog(hWnd, 0);
			break;
		case IDCANCEL:
			EndDialog(hWnd, 1);
			break;
		default:
			break;
			//return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code that uses hdc here...
		ShowImage(GetDlgItem(hWnd, IDC_DRAW_VIEW), tmpWatermark, WATERMARK_WIDTH, WATERMARK_HEIGHT);
		EndPaint(hWnd, &ps);
	}
	break;
	//case WM_DESTROY:
	//	PostQuitMessage(0);
	//	break;
		//default:
			//return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
	case WM_INITDIALOG:
		{
			hMainDlg = hWnd;

			HRESULT hr = amm.enumerateVideoDeviceSource();

			if (FAILED(hr)) {
				Warn_HR(hWnd, L"Unable to find any audio input device\n", hr);
				return 0;
			}

			amm.displayDevNames(GetDlgItem(hWnd, IDC_CMB_AUDIO_DEVICES));

			hr = vmm.enumerateVideoDeviceSource();

			if (FAILED(hr)) {
				Warn_HR(hWnd, L"Unable to find any video capture device\n", hr);
				return 0;
			}

			vmm.displayDevNames(GetDlgItem(hWnd, IDC_CMB_VIDEO_DEVICES));

			SetWindowTextA(GetDlgItem(hWnd, IDC_EDIT_FILE_NAME), szFile);

			hBusyAudio = CreateEventA(NULL, TRUE, TRUE, BUSY_AUDIO_STREAM);
			hBusyVideo = CreateEventA(NULL, TRUE, TRUE, BUSY_VIDEO_STREAM);
			hBusy = CreateEventA(NULL, TRUE, TRUE, WRITE_STATUS);

			SetEvent(hBusy);
			ResetEvent(hBusyAudio);
			ResetEvent(hBusyVideo);

			hAudioThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)receiveAudio, (LPVOID)hWnd, 0, NULL);
			hVideoThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)receiveVideo, (LPVOID)hWnd, 0, NULL);
			hMainThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)receiveFromMediaSources, (LPVOID)hWnd, 0, NULL);
		}
		break;
	case WM_DEVICECHANGE:
		if (lParam != 0)
		{
			HRESULT hr = S_OK;
			BOOL bDeviceLost = FALSE;

			hr = vmm.checkDeviceLost((PDEV_BROADCAST_HDR)lParam, &bDeviceLost);

			if (FAILED(hr) || bDeviceLost)
			{
				Warn_HR(hWnd, L"Lost the capture device.\n", hr);
			}

			hr = amm.checkDeviceLost((PDEV_BROADCAST_HDR)lParam, &bDeviceLost);

			if (FAILED(hr) || bDeviceLost)
			{
				Warn_HR(hWnd, L"Lost the capture device.\n", hr);
			}
		}
		return TRUE;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
			case IDC_DRAW:
				DialogBoxA(hInst, MAKEINTRESOURCEA(IDD_DLG_DRAW), hWnd, (DLGPROC)DrawDlgProc);
				break;
			case IDC_CMB_AUDIO_DEVICES:
				{
					int nSel = (int)SendMessage(GetDlgItem(hWnd, IDC_CMB_AUDIO_DEVICES), CB_GETCURSEL, 0, 0);
					int nCount = (int)SendMessage(GetDlgItem(hWnd, IDC_CMB_AUDIO_DEVICES), CB_GETCOUNT, 0, 0);

					if (nCount < 1) break;
					if (nSel < 0 || nSel >= nCount) break;
					if (nSel == nSelectedAUD) break;

					nSelectedAUD = nSel;
				}
				break;
			case IDC_CMB_VIDEO_DEVICES:
				{
					int nSel = (int)SendMessage(GetDlgItem(hWnd, IDC_CMB_VIDEO_DEVICES), CB_GETCURSEL, 0, 0);
					int nCount = (int)SendMessage(GetDlgItem(hWnd, IDC_CMB_VIDEO_DEVICES), CB_GETCOUNT, 0, 0);

					if (nCount < 1) break;
					if (nSel < 0 || nSel >= nCount) break;
					if (nSel == nSelectedVID) break;

					nSelectedVID = nSel;
				}
				break;
			case IDOK:
				if (nSelectedAUD == -1 || nSelectedVID == -1) break;
				if (bRecord == false) {
					if (InitMedia() == false) {
						MessageBox(hWnd, L"Unable to Initialize Media!", L"Error", MB_OK);
						break;
					}
					
					SetWindowText(GetDlgItem(hWnd, IDOK), L"Stop");
					bRecord = true;
				}
				else {
					bRecord = false;
					WaitForSingleObject(hBusy, INFINITE);
					CloseMedia();
					SetWindowText(GetDlgItem(hWnd, IDOK), L"Start");
				}
				break;
			case IDC_BROWSE:
			{
				OPENFILENAMEA ofn;

				ZeroMemory(&ofn, sizeof(ofn));
				ofn.lStructSize = sizeof(ofn);
				ofn.lpstrFile = szFile;
				ofn.lpstrFile[0] = TEXT('\0');
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "Video\0*.mp4";
				ofn.nFilterIndex = 1;
				ofn.lpstrFileTitle = NULL;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = NULL;
				ofn.hwndOwner = hWnd;
				ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

				GetSaveFileNameA(&ofn);

				if (strlen(szFile) < 5) {
					getWorkDir();
					break;
				}

				char *tmp = szFile + strlen(szFile) - 4;

				if (strstr(tmp, ".mp4") == NULL || strstr(tmp, ".Mp4") == NULL
					|| strstr(tmp, ".mP4") == NULL || strstr(tmp, ".MP4") == NULL) {
					sprintf(szFile, "%s.mp4", szFile);
				}

				SetWindowTextA(GetDlgItem(hWnd, IDC_EDIT_FILE_NAME), szFile);
			}
			break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDCANCEL:
                DestroyWindow(hWnd);
				exit(0);
                break;
            default:
				break;
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
		exit(0);
        break;
    //default:
        //return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
