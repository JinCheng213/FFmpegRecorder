#include "framework.h"
#include "FFmpegRecoder.h"

#define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |       \
                ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))
#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// RGB -> YUV
#define RGB2Y(R, G, B) CLIP(( (  66 * (R) + 129 * (G) +  25 * (B) + 128) >> 8) +  16)
#define RGB2U(R, G, B) CLIP(( ( -38 * (R) -  74 * (G) + 112 * (B) + 128) >> 8) + 128)
#define RGB2V(R, G, B) CLIP(( ( 112 * (R) -  94 * (G) -  18 * (B) + 128) >> 8) + 128)

// YUV -> RGB
#define C(Y) ( (Y) - 16  )
#define D(U) ( (U) - 128 )
#define E(V) ( (V) - 128 )

#define YUV2R(Y, U, V) CLIP(( 298 * C(Y)              + 409 * E(V) + 128) >> 8)
#define YUV2G(Y, U, V) CLIP(( 298 * C(Y) - 100 * D(U) - 208 * E(V) + 128) >> 8)
#define YUV2B(Y, U, V) CLIP(( 298 * C(Y) + 516 * D(U)              + 128) >> 8)

// RGB -> YCbCr
#define CRGB2Y(R, G, B) CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16)
#define CRGB2Cb(R, G, B) CLIP((36962 * (B - CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16) ) >> 16) + 128)
#define CRGB2Cr(R, G, B) CLIP((46727 * (R - CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16) ) >> 16) + 128)

// YCbCr -> RGB
#define CYCbCr2R(Y, Cb, Cr) CLIP( Y + ( 91881 * Cr >> 16 ) - 179 )
#define CYCbCr2G(Y, Cb, Cr) CLIP( Y - (( 22544 * Cb + 46793 * Cr ) >> 16) + 135)
#define CYCbCr2B(Y, Cb, Cr) CLIP( Y + (116129 * Cb >> 16 ) - 226 )

VIDEO_MS_MANAGER vmm;

static void TransformImage_UYVY(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD dwWidthInPixels, DWORD dwHeightInPixels)
{
	BYTE *pSrc_Pixel = pSrc;
	BYTE *pDst_Pixel = pDest;

	for (DWORD y = 0; y < height; y++)
	{
		for (DWORD x = 0; x < width / 2; x++)
		{
			int u0 = pSrc_Pixel[0];
			int y0 = pSrc_Pixel[1];
			int v0 = pSrc_Pixel[2];
			int y1 = pSrc_Pixel[3];

			pSrc_Pixel += 4;

			int c = y0 - 16;
			int d = u0 - 128;
			int e = v0 - 128;

			int b = (298 * c + 516 * d + 128) >> 8; // blue
			int g = (298 * c - 100 * d - 208 * e + 128) >> 8; // green
			int r = (298 * c + 409 * e + 128) >> 8; // red

			b = b < 0 ? 0 : b; b = b > 255 ? 255 : b;
			g = g < 0 ? 0 : g; g = g > 255 ? 255 : g;
			r = r < 0 ? 0 : r; r = r > 255 ? 255 : r;
			// Byte order is Y0 U0 Y1 V0 
			// Each WORD is a byte pair (Y, U/V)
			// Windows is little-endian so the order appears reversed.

			pDst_Pixel[0] = (BYTE)b;
			pDst_Pixel[1] = (BYTE)g;
			pDst_Pixel[2] = (BYTE)r;
			pDst_Pixel[3] = 255;

			c = y1 - 16;

			b = (298 * c + 516 * d + 128) >> 8; // blue
			g = (298 * c - 100 * d - 208 * e + 128) >> 8; // green
			r = (298 * c + 409 * e + 128) >> 8; // red

			b = b < 0 ? 0 : b; b = b > 255 ? 255 : b;
			g = g < 0 ? 0 : g; g = g > 255 ? 255 : g;
			r = r < 0 ? 0 : r; r = r > 255 ? 255 : r;

			pDst_Pixel[4] = (BYTE)b;
			pDst_Pixel[5] = (BYTE)g;
			pDst_Pixel[6] = (BYTE)r;
			pDst_Pixel[7] = 255;

			pDst_Pixel += 8;
		}
	}
}

static void TransformImage_UYVY2YUV(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height)
{
	for (DWORD y = 0; y < height; y++)
	{
		BYTE *pSrc_Pixel = pSrc + y * width * 2;
		BYTE *pDestY = pDest + y * width;
		BYTE *pDestUV = pDest + height * width + (y / 2) * width;
		for (DWORD x = 0; x < width; x++)
		{
			BYTE y = pSrc_Pixel[x * 2] & 0x0F | ((pSrc_Pixel[x * 2 + 1] & 0x0F) << 4);
			BYTE v = (pSrc_Pixel[x * 2 + 1] & 0xF0) >> 4;
			BYTE u = (pSrc_Pixel[x * 2] & 0xF0) >> 4;
			pDestY[x] = y;
			pDestUV[(x / 2) * 2] = v;
			pDestUV[(x / 2) * 2 + 1] = u;
		}
	}
}

static void TransformImage_YUY2(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height)
{
	BYTE *pSrc_Pixel = pSrc;
	BYTE *pDst_Pixel = pDest;

	for (DWORD y = 0; y < height; y++)
	{
		for (DWORD x = 0; x < width / 2; x++)
		{
			int y0 = pSrc_Pixel[0];
			int u0 = pSrc_Pixel[1];
			int y1 = pSrc_Pixel[2];
			int v0 = pSrc_Pixel[3];
			
			pSrc_Pixel += 4;

			int c = y0 - 16;
			int d = u0 - 128;
			int e = v0 - 128;

			int b = (298 * c + 516 * d + 128) >> 8; // blue
			int g = (298 * c - 100 * d - 208 * e + 128) >> 8; // green
			int r = (298 * c + 409 * e + 128) >> 8; // red

			b = b < 0 ? 0 : b; b = b > 255 ? 255 : b;
			g = g < 0 ? 0 : g; g = g > 255 ? 255 : g;
			r = r < 0 ? 0 : r; r = r > 255 ? 255 : r;
			// Byte order is Y0 U0 Y1 V0 
			// Each WORD is a byte pair (Y, U/V)
			// Windows is little-endian so the order appears reversed.

			pDst_Pixel[0] = (BYTE)b;
			pDst_Pixel[1] = (BYTE)g;
			pDst_Pixel[2] = (BYTE)r;
			pDst_Pixel[3] = 255;

			c = y1 - 16;

			b = (298 * c + 516 * d + 128) >> 8; // blue
			g = (298 * c - 100 * d - 208 * e + 128) >> 8; // green
			r = (298 * c + 409 * e + 128) >> 8; // red

			b = b < 0 ? 0 : b; b = b > 255 ? 255 : b;
			g = g < 0 ? 0 : g; g = g > 255 ? 255 : g;
			r = r < 0 ? 0 : r; r = r > 255 ? 255 : r;

			pDst_Pixel[4] = (BYTE)b;
			pDst_Pixel[5] = (BYTE)g;
			pDst_Pixel[6] = (BYTE)r;
			pDst_Pixel[7] = 255;

			pDst_Pixel += 8;
		}
	}
}

static void TransformImage_YUY2_YUV(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height)
{
	for (DWORD y = 0; y < height; y++)
	{
		BYTE *pSrc_Pixel = pSrc + y * width * 2;
		BYTE *pDestY = pDest + y * width;
		BYTE *pDestUV = pDest + height * width + (y / 2) * width; 

		for (DWORD x = 0; x < width; x++)
		{
			BYTE y = (pSrc_Pixel[x * 2] & 0xF0) >> 4 | (pSrc_Pixel[x * 2 + 1] & 0xF0);
			BYTE v = pSrc_Pixel[x * 2 + 1] & 0x0F;
			BYTE u = pSrc_Pixel[x * 2] & 0x0F;

			// Byte order is Y0 U0 Y1 V0 
			// Each WORD is a byte pair (Y, U/V)
			// Windows is little-endian so the order appears reversed.
			pDestY[x] = y;
			pDestUV[(x / 2) * 2] = v;
			pDestUV[(x / 2) * 2 + 1] = u;
		}
	}
}

static void TransformImage_NV12(BYTE* pDest, BYTE* pSrc, LONG lSrcStride, DWORD width, DWORD height)
{
	// NV12 is planar: Y plane, followed by packed U-V plane.

	// Y plane
	for (DWORD y = 0; y < height; y++)
	{
		BYTE *pSrcY = pSrc + y * width;
		BYTE *pSrcUV = pSrc + width * height;
		pSrcUV += (y / 2) * width;
		BYTE *pDest_Pixel = pDest + y * width * 4;
		for (DWORD x = 0; x < width; x++)
		{
			float y = (float)pSrcY[x];
			float v = (float)(pSrcUV[(x / 2) * 2]);
			float u = (float)(pSrcUV[(x / 2) * 2 + 1]);

			float b = (float)(1.164 * (y - 16) + 2.018 * (u - 128));
			float g = (float)(1.164 * (y - 16) - 0.813 * (v - 128) - 0.391 * (u - 128));
			float r = (float)(1.164 * (y - 16) + 1.596*(v - 128));

			b = b < 0 ? 0 : b; b = b > 255 ? 255 : b;
			g = g < 0 ? 0 : g; g = g > 255 ? 255 : g;
			r = r < 0 ? 0 : r; r = r > 255 ? 255 : r;

			pDest_Pixel[x * 4] = (BYTE)r;
			pDest_Pixel[x * 4 + 1] = (BYTE)g;
			pDest_Pixel[x * 4 + 2] = (BYTE)b;
			pDest_Pixel[x * 4 + 3] = 255;
		}
	}
}

static void TransformImage_RGB_NV12(BYTE* pDest, BYTE* pSrc, DWORD width, DWORD height)
{
	for (DWORD i = 0; i < height; i++) {
		BYTE *pDstY = pDest + i * width;
		BYTE *pDstUV = pDest + width * height;
		pDstUV += (i / 2) * width;
		BYTE *ptr = pSrc + i * width * 4;
		for (DWORD j = 0; j < width; j++) {
			BYTE r = ptr[j * 4];
			BYTE g = ptr[j * 4 + 1];
			BYTE b = ptr[j * 4 + 2];

			BYTE y = RGB2Y(r, g, b);
			BYTE u = RGB2U(r, g, b);
			BYTE v = RGB2V(r, g, b);

			pDstY[j] = y;
			pDstUV[(j / 2) * 2] = v;
			pDstUV[(j / 2) * 2 + 1] = u;
		}
	}
}

HRESULT VIDEO_MS_MANAGER::enumerateVideoDeviceSource()
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
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
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
	if(FAILED(hr)) release();
	CoUninitialize();
	return hr;
}

HRESULT VIDEO_MS_MANAGER::initMediaSourceByIndex(UINT32 index, HWND hWnd) {
	PROPVARIANT		var;
	BOOL			fSelected;
	DEV_BROADCAST_DEVICEINTERFACE	di = { 0 };
	DWORD cTypes = 0;
	UINT32 quality = 0;
	UINT32 hqIndex = -1;

	CoInitialize(NULL);

	HRESULT	hr = activeMediaSourceByIndex(index);

	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to activate this video device.\n", hr);
		goto _final;
	}

	hr = ppDevices[index]->GetAllocatedString(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
		&g_pwszSymbolicLink,
		&g_cchSymbolicLink
	);

	if (FAILED(hr)) {
		Warn_HR(hWnd, L"Unable access to symbolic link. (video device)\n", hr);
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
		Warn_HR(hWnd, L"Unable to register notification. (video device)\n", hr);
		goto _final;
	}

	hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to create presentaion descriptor. (video device)\n", hr);
		goto _final;
	}


	hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to get stream descriptor. (video device)\n", hr);
		goto _final;
	}

	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to get media type handler. (video device)", hr);
		goto _final;
	}

	hr = pHandler->GetMediaTypeCount(&cTypes);
	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to get media type count. (video device)", hr);
		goto _final;
	}

	for (DWORD i = 0; i < cTypes; i++)
	{
		hr = pHandler->GetMediaTypeByIndex(i, &pType);
		if (FAILED(hr)) continue;

		hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);

		if (SUCCEEDED(hr)) {
			if(width * height > quality) {
				quality = width * height;
				hqIndex = i;
			}
			SafeRelease(&pType);
		}
	}

	hr = pHandler->GetMediaTypeByIndex(hqIndex, &pType);

	if (FAILED(hr))
	{
		Warn_HR(hWnd, L"Unable to get media type. (video device)", hr);
		goto _final;
	}

	hr = pHandler->SetCurrentMediaType(pType);

	if (FAILED(hr)) {
		Warn_HR(hWnd, L"Unable to set current media type. (video device)", hr);
		goto _final;
	}

	// Get the maximum frame rate for the selected capture format.

	// Note: To get the minimum frame rate, use the 
	// MF_MT_FRAME_RATE_RANGE_MIN attribute instead.

	if (SUCCEEDED(pType->GetItem(MF_MT_FRAME_RATE_RANGE_MAX, &var)))
	{
		hr = pType->SetItem(MF_MT_FRAME_RATE, var);

		PropVariantClear(&var);

		if (FAILED(hr))
		{
			goto _final;
		}

		hr = pHandler->SetCurrentMediaType(pType);
	}

	hr = MFCreateSourceReaderFromMediaSource(pSource,
		pAttributes, &pSourceReader);

	if (FAILED(hr)) {
		goto _final;
	}

	pSourceReader->AddRef();

	hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);

	if (FAILED(hr)) goto _final;

	hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);

	if (FAILED(hr)) goto _final;
	//hr = configureVideoStream(pTypeBase);
	
	pRgbBuffer = (BYTE *)malloc(width * height * 4);
	pYuvBuffer = (BYTE *)malloc(width * height * 3 / 2);

	hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);

_final:
	if (FAILED(hr)) {
		this->releaseMediaSource();
	}
	CoUninitialize();
	return hr;
}

HRESULT VIDEO_MS_MANAGER::configureVideoStream(IMFMediaType *pTypeBase)
{
	IMFMediaType *pUncompressedAudioType = NULL;
	IMFMediaType *pPartialType = NULL;
	PROPVARIANT	var;

	// Select the first audio stream, and deselect all other streams.
	HRESULT hr = pSourceReader->SetStreamSelection(
		(DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);

	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->SetStreamSelection(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
	}
	
	// Create a partial media type that specifies uncompressed PCM audio.
	hr = MFCreateMediaType(&pPartialType);

	if (SUCCEEDED(hr))
	{
		hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	}

	if (SUCCEEDED(hr))
	{
		hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_UYVY);
	}

	if (SUCCEEDED(pTypeBase->GetItem(MF_MT_FRAME_RATE_RANGE_MAX, &var)))
	{
		hr = pPartialType->SetItem(MF_MT_FRAME_RATE, var);
	}

	if (SUCCEEDED(hr)) {
		hr = MFGetAttributeSize(pTypeBase, MF_MT_FRAME_SIZE, &width, &height);
		if (SUCCEEDED(hr)) {
			MFSetAttributeSize(pPartialType, MF_MT_FRAME_SIZE, width, height);
		}
	}

	// Set this type on the source reader. The source reader will
	// load the necessary decoder.
	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->SetCurrentMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
			NULL, pPartialType);
	}

	// Get the complete uncompressed format.
	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->GetCurrentMediaType(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
			&pUncompressedAudioType);
	}

	// Ensure the stream is selected.
	if (SUCCEEDED(hr))
	{
		hr = pSourceReader->SetStreamSelection(
			(DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
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

HRESULT VIDEO_MS_MANAGER::activeMediaSourceByIndex(UINT32 index) {
	CoInitialize(NULL);
	HRESULT	hr = ppDevices[index]->ActivateObject(IID_PPV_ARGS(&pSource));

	if(SUCCEEDED(hr)) pSource->AddRef();
	CoUninitialize();
	return hr;
}

HRESULT VIDEO_MS_MANAGER::checkDeviceLost(DEV_BROADCAST_HDR *pHdr, BOOL *pbDeviceLost)
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

HRESULT VIDEO_MS_MANAGER::getDefaultStride(LONG *plStride)
{
	LONG lStride = 0;

	CoInitialize(NULL);

	// Try to get the default stride from the media type.
	HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
	if (FAILED(hr))
	{
		// Attribute not set. Try to calculate the default stride.

		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Get the subtype and the image size.
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (FAILED(hr))
		{
			goto done;
		}

		hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
		if (FAILED(hr))
		{
			goto done;
		}

		hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
		if (FAILED(hr))
		{
			goto done;
		}

		// Set the attribute for later reference.
		(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
	}

	if (SUCCEEDED(hr))
	{
		*plStride = lStride;
	}

done:
	CoUninitialize();
	return hr;
}

HRESULT VIDEO_MS_MANAGER::readFrame() {
	DWORD		streamIndex, flags;
	LONGLONG	llTimeStamp;

	DWORD		bufLength = 0;
	LONG		lDefaultStride = 0;

	CoInitialize(NULL);

	HRESULT hr = pSourceReader->ReadSample(
		MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		0,                              // Flags.
		&streamIndex,                   // Receives the actual stream index. 
		&flags,                         // Receives status flags.
		&llTimeStamp,                   // Receives the time stamp.
		&pVideoSample                    // Receives the sample or NULL.
	);

	if (FAILED(hr) || pVideoSample == NULL) goto _final;

	hr = pVideoSample->ConvertToContiguousBuffer(&input);

	if (FAILED(hr)) goto _final;

	hr = input->Lock(&pRawBuf, NULL, &bufLength);

	if (FAILED(hr)) goto _final;

	hr = getDefaultStride(&lDefaultStride);

	if (FAILED(hr)) goto _final;

	switch (subtype.Data1)
	{
	case MAKEFOURCC('Y', 'U', 'Y', '2'):
		TransformImage_YUY2(pRgbBuffer, pRawBuf, lDefaultStride, width, height);
		//TransformImage_YUY2_YUV(pYuvBuffer, pRawBuf, lDefaultStride, width, height);
		break;
	case MAKEFOURCC('U', 'Y', 'V', 'Y'):
		TransformImage_UYVY(pRgbBuffer, pRawBuf, lDefaultStride, width, height);
		//TransformImage_UYVY2YUV(pYuvBuffer, pRawBuf, lDefaultStride, width, height);
		break;
	case MAKEFOURCC('N', 'V', '1', '2'):
		TransformImage_NV12(pRgbBuffer, pRawBuf, lDefaultStride, width, height);
		//memcpy(pYuvBuffer, pRawBuf, width * height * 3 / 2);
		break;
	default:
		bStopFlag = true;
		goto _final;
	}

	{
		float sx = (float)WATERMARK_WIDTH / (float)width;
		float sy = (float)WATERMARK_HEIGHT / (float)height;

		for (DWORD i = 0; i < height; i++) {
			int ii = (int)((float)i * sy);
			BYTE *p1 = waterMark + ii * 4 * WATERMARK_WIDTH;
			BYTE *p2 = pRgbBuffer + (int)((float)i * 4 * width);
			for (DWORD j = 0; j < width; j++) {
				int jj = (int)((float)j * sx);
				if (p1[jj * 4 + 2] != 255) continue;
				
				p2[j * 4] = 0;
				p2[j * 4 + 1] = 0;
				p2[j * 4 + 2] = 255;
			}
		}

		TransformImage_RGB_NV12(pYuvBuffer, pRgbBuffer, width, height);
	}
	input->Unlock();

_final:
	SafeRelease(&input);
	SafeRelease(&pVideoSample);
	CoUninitialize();
	return hr;
}

void VIDEO_MS_MANAGER::displayDevNames(HWND hWnd) {
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

void VIDEO_MS_MANAGER::releaseMediaSource() {
	if (pYuvBuffer) {
		free(pYuvBuffer);
		pYuvBuffer = NULL;
	}

	if (pRgbBuffer) {
		free(pRgbBuffer);
		pRgbBuffer = NULL;
	}

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

void VIDEO_MS_MANAGER::release() {
	this->releaseMediaSource();
	SafeRelease(&pAttributes);

	for (DWORD i = 0; i < uiDevCount; i++)
	{
		SafeRelease(&ppDevices[i]);
	}
	CoTaskMemFree(ppDevices);
}
