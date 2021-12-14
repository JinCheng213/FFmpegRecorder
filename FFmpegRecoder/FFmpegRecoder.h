#pragma once

#include "resource.h"
#include "framework.h"

#include <mfapi.h>
#include <mfidl.h>
#include <Dbt.h>
#include <ks.h>
#include <mfreadwrite.h>
#include <commctrl.h>
#include <comdef.h>

#define UINT64_C(x)  (x ## ULL)
#define INP_BUFFER_SIZE		1024

#define WRITE_STATUS			"WRITE"
#define CAPTURE_STATUS			"CAPTURE"
#define AUDIO_STATUS			"AUD_CAPTURE"
#define WATERMARK_WIDTH		200
#define WATERMARK_HEIGHT	200

typedef struct _VIDEO_MS_MANAGER_
{
	IMFMediaSource	*pSource = NULL;
	IMFAttributes	*pAttributes = NULL;
	IMFActivate		**ppDevices = NULL;
	WCHAR			*g_pwszSymbolicLink = NULL;
	UINT32			g_cchSymbolicLink = 0;
	HDEVNOTIFY		g_hdevnotify = NULL;
	GUID			subtype;

	IMFMediaType					*pType = NULL;
	IMFPresentationDescriptor		*pPD = NULL;
	IMFStreamDescriptor				*pSD = NULL;
	IMFMediaTypeHandler				*pHandler = NULL;
	IMFSourceReader					*pSourceReader = NULL;
	IMFMediaBuffer*					input = NULL;
	IMFSample*						pVideoSample = NULL;

	UINT32	uiDevCount = 0;
	BYTE	*pRgbBuffer = NULL;
	BYTE	*pRawBuf = NULL;
	BYTE	*pYuvBuffer = NULL;

	HRESULT configureVideoStream(IMFMediaType *pType);
	HRESULT	enumerateVideoDeviceSource();
	HRESULT	initMediaSourceByIndex(UINT32 index, HWND hWnd);
	HRESULT	checkDeviceLost(DEV_BROADCAST_HDR *pHdr, BOOL *pbDeviceLost);
	HRESULT	activeMediaSourceByIndex(UINT32 index);
	HRESULT	getDefaultStride(LONG *plStride);
	HRESULT	readFrame();

	void	displayDevNames(HWND hWnd);
	void	releaseMediaSource();
	void	release();
}VIDEO_MS_MANAGER, *PVIDEO_MS_MANAGER;

typedef struct _AUDIO_MS_MANAGER_
{
	IMFMediaSource	*pSource = NULL;
	IMFAttributes	*pAttributes = NULL;
	IMFActivate		**ppDevices = NULL;
	WCHAR			*g_pwszSymbolicLink = NULL;
	UINT32			g_cchSymbolicLink = 0;
	HDEVNOTIFY		g_hdevnotify = NULL;
	GUID			subtype;

	DEV_BROADCAST_DEVICEINTERFACE	di = { 0 };
	IMFMediaType					*pType = NULL;
	IMFPresentationDescriptor		*pPD = NULL;
	IMFStreamDescriptor				*pSD = NULL;
	IMFMediaTypeHandler				*pHandler = NULL;
	IMFSourceReader					*pSourceReader = NULL;
	IMFMediaBuffer*					input = NULL;
	IMFSample*						pAudioSample = NULL;
	IMFMediaType*					pMediaType = NULL;

	UINT32	uiDevCount = 0;
	BYTE	*pRawBuffer = NULL;
	BYTE	pAudioBuffer[8000];

	UINT32	cChannels = 0;
	UINT32	samplesPerSec = 0;
	UINT32	bitsPerSample = 32;
	DWORD	dwbufLength = 0;

	HRESULT	enumerateVideoDeviceSource();
	HRESULT	initMediaSourceByIndex(UINT32 index, HWND hWnd);
	HRESULT	checkDeviceLost(DEV_BROADCAST_HDR *pHdr, BOOL *pbDeviceLost);
	HRESULT	activeMediaSourceByIndex(UINT32 index);
	HRESULT configureAudioStream();
	HRESULT	readFrame();

	void	displayDevNames(HWND hWnd);
	void	releaseMediaSource();
	void	release();
}AUDIO_MS_MANAGER, *PAUDIO_MS_MANAGER;

extern char		szFile[MAX_PATH];
extern UINT32	width;
extern UINT32	height;
extern HANDLE	hBusy;
extern HWND		hMainDlg;

extern bool		bFoundRecDevice;
extern bool		bStopFlag;
extern bool		bAudioEnabled;

extern VIDEO_MS_MANAGER vmm;
extern AUDIO_MS_MANAGER amm;

extern BYTE		lpAudioBlock[INP_BUFFER_SIZE];
extern DWORD	dwAudioBlockLength;

extern BYTE	waterMark[4 * WATERMARK_WIDTH * WATERMARK_HEIGHT];

bool InitMedia();
int WriteMedia();
void CloseMedia();

static void TransformImage_UYVY(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height);
static void TransformImage_YUY2(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height);
static void TransformImage_NV12(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height);

void Warn_HR(HWND hWnd, const TCHAR *msg, HRESULT hr);

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}