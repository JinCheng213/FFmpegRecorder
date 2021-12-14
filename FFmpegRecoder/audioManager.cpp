#include "framework.h"
#include "FFmpegRecoder.h"

AUDIO_MS_MANAGER amm;
HANDLE	hFile;
DWORD cbHeader = 0;

HRESULT AUDIO_MS_MANAGER::enumerateVideoDeviceSource()
{
	CoInitialize(NULL);
	// Create an attribute store to specify the enumeration parameters.
	HRESULT hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		goto done;
	}

	// Source type: video capture devices
	hr = pAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID
	);
	if (FAILED(hr))
	{
		goto done;
	}

	// Enumerate devices.
	hr = MFEnumDeviceSources(pAttributes, &ppDevices, &uiDevCount);
	if (FAILED(hr))
	{
		goto done;
	}

	if (uiDevCount == 0)
	{
		hr = E_FAIL;
		goto done;
	}

done:
	if (FAILED(hr))	release();
	CoUninitialize();
	return hr;
}

void AUDIO_MS_MANAGER::displayDevNames(HWND hWnd) {
	CoInitialize(NULL);
	for (int i = 0; i < (int)uiDevCount; i++) {
		UINT32	length = 256;
		WCHAR	*g_pwszFriendlyName = NULL;
		UINT32	g_cchSymbolicLink = 0;

		HRESULT hr = ppDevices[i]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&g_pwszFriendlyName,
			&g_cchSymbolicLink
		);

		if (FAILED(hr)) continue;

		SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM)g_pwszFriendlyName);
	}
	CoUninitialize();
}

HRESULT AUDIO_MS_MANAGER::activeMediaSourceByIndex(UINT32 index) {
	CoInitialize(NULL);

	HRESULT hr = ppDevices[index]->ActivateObject(IID_PPV_ARGS(&pSource));

	if (SUCCEEDED(hr)) pSource->AddRef();
	CoUninitialize();
	return hr;
}

HRESULT AUDIO_MS_MANAGER::initMediaSourceByIndex(UINT32 index, HWND hWnd) {
	DWORD	typeIndex = -1;
	DWORD	cTypes = 0;
	DEV_BROADCAST_DEVICEINTERFACE	di = { 0 };

	CoInitialize(NULL);

	HRESULT	hr = activeMediaSourceByIndex(index);

	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to activate this audio device.\n", hr);
		goto _final;
	}

	hr = ppDevices[index]->GetAllocatedString(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_SYMBOLIC_LINK,
		&g_pwszSymbolicLink,
		&g_cchSymbolicLink
	);

	if (FAILED(hr)) {
		Warn_HR(hWnd, L"Unable to access to symbolic link for this audio device.\n", hr);
		goto _final;
	}

	di.dbcc_size = sizeof(di);
	di.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	di.dbcc_classguid = KSCATEGORY_CAPTURE;

	g_hdevnotify = RegisterDeviceNotification(
		hWnd,
		&di,
		DEVICE_NOTIFY_WINDOW_HANDLE
	);

	if (g_hdevnotify == NULL)
	{
		Warn_HR(hWnd, L"Unable to register notification for this audio device.\n", hr);
		goto _final;
	}

	hr = MFCreateSourceReaderFromMediaSource(pSource,
		pAttributes, &pSourceReader);

	if (FAILED(hr)) {
		Warn_HR(hWnd, L"Unable to access to Media Source Reader (audio device).\n", hr);
		goto _final;
	}
	
	configureAudioStream();

	hr = pSourceReader->GetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
		&pType);

	pType->GetGUID(MF_MT_SUBTYPE, &subtype);

	if (subtype == MFAudioFormat_PCM) {
		pType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
		pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &cChannels);
		pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_BLOCK, &samplesPerSec);
	}

	pSourceReader->AddRef();

_final:
	if (FAILED(hr)) {
		this->releaseMediaSource();
	}
	CoUninitialize();
	return hr;
}

HRESULT AUDIO_MS_MANAGER::configureAudioStream()
{
	IMFMediaType *pUncompressedAudioType = NULL;
	IMFMediaType *pPartialType = NULL;

	// Select the first audio stream, and deselect all other streams.
	HRESULT hr = pSourceReader->SetStreamSelection(
		(DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);

	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->SetStreamSelection(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
	}

	// Create a partial media type that specifies uncompressed PCM audio.
	hr = MFCreateMediaType(&pPartialType);

	if (SUCCEEDED(hr))
	{
		hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	}

	if (SUCCEEDED(hr))
	{
		hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		
		hr = pPartialType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 8);
		hr = pPartialType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
		hr = pPartialType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 11025);
		hr = pPartialType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 11025);
		hr = pPartialType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
	}

	// Set this type on the source reader. The source reader will
	// load the necessary decoder.
	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->SetCurrentMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			NULL, pPartialType);
	}

	// Get the complete uncompressed format.
	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->GetCurrentMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			&pUncompressedAudioType);
	}

	// Ensure the stream is selected.
	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->SetStreamSelection(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			TRUE);
	}

	// Return the PCM format to the caller.
	if (SUCCEEDED(hr))
	{
		pType = pUncompressedAudioType;
		(pType)->AddRef();
	}

	SafeRelease(&pUncompressedAudioType);
	SafeRelease(&pPartialType);

	return hr;
}

HRESULT AUDIO_MS_MANAGER::checkDeviceLost(DEV_BROADCAST_HDR *pHdr, BOOL *pbDeviceLost)
{
	DEV_BROADCAST_DEVICEINTERFACE *pDi = NULL;

	if (pbDeviceLost == NULL)
	{
		return E_POINTER;
	}

	*pbDeviceLost = FALSE;

	if (pHdr == NULL)
	{
		return S_OK;
	}
	if (pHdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
	{
		return S_OK;
	}

	// Compare the device name with the symbolic link.

	pDi = (DEV_BROADCAST_DEVICEINTERFACE*)pHdr;

	if (g_pwszSymbolicLink)
	{
		if (_wcsicmp(g_pwszSymbolicLink, pDi->dbcc_name) == 0)
		{
			*pbDeviceLost = TRUE;
		}
	}

	return S_OK;
}

HRESULT AUDIO_MS_MANAGER::readFrame() {
	DWORD		dwFlags = 0;
	DWORD		bufLength = 0;
	CoInitialize(NULL);
	static DWORD lll = 0;

	HRESULT hr = pSourceReader->ReadSample(
		(DWORD)MF_SOURCE_READER_ANY_STREAM,
		0, NULL, &dwFlags, NULL, &pAudioSample);

	if (dwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
	{
		MessageBox(hMainDlg, L"Type change - not supported by WAVE file format.", L"Error", MB_OK);
		hr = S_FALSE;
		goto _final;
	}
	if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM)
	{
		MessageBox(hMainDlg, L"End of input file", L"Error", MB_OK);
		hr = S_FALSE;
		goto _final;
	}

	if (FAILED(hr) || pAudioSample == NULL) goto _final;

	hr = pAudioSample->ConvertToContiguousBuffer(&input);

	if (FAILED(hr)) goto _final;

	hr = input->Lock(&pRawBuffer, NULL, &bufLength);

	if (FAILED(hr)) goto _final;

	if (this->dwbufLength + bufLength < 8000) {
		memcpy(pAudioBuffer + this->dwbufLength, pRawBuffer, bufLength);
		this->dwbufLength += bufLength;
	}

	dwAudioBlockLength = bufLength > INP_BUFFER_SIZE ? INP_BUFFER_SIZE : bufLength;
	memcpy(lpAudioBlock, pRawBuffer, dwAudioBlockLength);

	input->Unlock();
	pRawBuffer = NULL;
_final:
	SafeRelease(&input);
	SafeRelease(&pAudioSample);
	CoUninitialize();
	return hr;
}

void AUDIO_MS_MANAGER::release() {
	this->releaseMediaSource();
	SafeRelease(&pAttributes);

	for (DWORD i = 0; i < uiDevCount; i++)
	{
		SafeRelease(&ppDevices[i]);
	}
	CoTaskMemFree(ppDevices);
}

void AUDIO_MS_MANAGER::releaseMediaSource() {
	if (g_hdevnotify)
	{
		UnregisterDeviceNotification(g_hdevnotify);
	}
	SafeRelease(&pSource);
	SafeRelease(&pType);
	SafeRelease(&pPD);
	SafeRelease(&pSD);
	SafeRelease(&pHandler);
	SafeRelease(&pSourceReader);
}


