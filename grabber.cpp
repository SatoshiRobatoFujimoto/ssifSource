#include "stdafx.h"
#include "utils.h"
#include "grabber.h"

// This source file is a changed version of this example:
// http://mobilaboratory.googlecode.com/svn-history/r23/trunk/CameraCapture/SampleGrabberFilterComplete/SampleGrabber.cpp

#define FILTERNAME L"SampleGrabber"

const GUID *avisynth_accept_formats[] = {
    &MEDIASUBTYPE_YV12,
    &MEDIASUBTYPE_I420,
    &MFVideoFormat_YUY2,
    &MEDIASUBTYPE_RGB32,
    &MEDIASUBTYPE_RGB24
};
const int avisynth_equivalents[] = {
    VideoInfo::CS_YV12,
    VideoInfo::CS_I420,
    VideoInfo::CS_YUY2,
    VideoInfo::CS_BGR32,
    VideoInfo::CS_BGR24
};
int avisynth_accept_formats_count = sizeof(avisynth_accept_formats) / sizeof(const GUID*);

CSampleGrabber::CSampleGrabber(IUnknown* pOuter, HRESULT* phr, BOOL ModifiesData)
: CTransInPlaceFilter(FILTERNAME, pOuter, CLSID_SampleGrabber, phr)
{
    m_SampleSize      = 0;
    m_pAM_MEDIA_TYPE  = NULL;
    m_Width           = 0;
    m_Height          = 0;
    m_Stride          = 0;
    m_FrameSize       = 0;
    memset(&avisynth_vi, 0, sizeof(VideoInfo));
    m_AvgTimePerFrame = 0;
    hDataReady = CreateEvent(NULL, TRUE, FALSE, NULL);
    hDataParsed = CreateEvent(NULL, TRUE, FALSE, NULL);
    nFrame = 0;
}

CSampleGrabber::~CSampleGrabber() {
    CloseSyncHandles();
}

void CSampleGrabber::CloseSyncHandles() {
    SetEvent(hDataParsed);
    SAFE_CloseHandle(hDataReady);
    SAFE_CloseHandle(hDataParsed);
}

HRESULT CSampleGrabber::CheckInputType(const CMediaType* pmt) {
    VIDEOINFOHEADER *vih = (VIDEOINFOHEADER*)pmt->pbFormat;
    if (pmt->majortype == MEDIATYPE_Video && pmt->formattype == FORMAT_VideoInfo && vih->bmiHeader.biHeight > 0) {
        for(int i=0; i<avisynth_accept_formats_count; ++i) {
            if (pmt->subtype == *(avisynth_accept_formats[i])) {
                return S_OK;
            }
        }
    }
    return E_FAIL;
}

HRESULT CSampleGrabber::SetMediaType(PIN_DIRECTION direction, const CMediaType* pmt) {
    HRESULT hr = S_OK;
    VIDEOINFOHEADER *vih = (VIDEOINFOHEADER*)pmt->pbFormat;

    if (CheckInputType(pmt) == S_OK && pmt->cbFormat >= sizeof(VIDEOINFOHEADER) && pmt->pbFormat != NULL) {
        // fill AviSynth VideoInfo structure
        memset(&avisynth_vi, 0, sizeof(VideoInfo));
        avisynth_vi.pixel_type = 0;
        for(int i=0; i<avisynth_accept_formats_count; ++i) {
            if (pmt->subtype == *(avisynth_accept_formats[i])) {
                avisynth_vi.pixel_type = avisynth_equivalents[i];
                break;
            }
        }
        avisynth_vi.width = vih->bmiHeader.biWidth;
        avisynth_vi.height = vih->bmiHeader.biHeight;
        avisynth_vi.fps_numerator = 10 * 1000 * 1000;
        avisynth_vi.fps_denominator = (unsigned int)vih->AvgTimePerFrame;

        // avisynth_vi.num_frames  // can't fill yet
        avisynth_vi.audio_samples_per_second = 0;  // we don't support audio

        // save the media format
        m_mt = *pmt;
        m_Width = vih->bmiHeader.biWidth;
        m_Height = vih->bmiHeader.biHeight;
        m_SampleSize = pmt->lSampleSize;
        // calculate the stride for RGB formats
        DWORD dwStride = (vih->bmiHeader.biWidth * (vih->bmiHeader.biBitCount / 8) + 3) & ~3;
        m_Stride = (long)dwStride;
        m_AvgTimePerFrame = vih->AvgTimePerFrame;
    }
    else
        hr = E_FAIL;
    return hr;
}

HRESULT CSampleGrabber::Transform(IMediaSample* pMediaSample) {
    HRESULT hr = S_OK;
    long lSize = 0;
    double dTimestamp = 0;
    BYTE *pCurrentBits;

    if (!pMediaSample)
        return E_FAIL;
    if (FAILED(pMediaSample->GetPointer(&pCurrentBits)))
        goto Cleanup;
    lSize = pMediaSample->GetSize();

    if (m_FrameSize == 0)
        m_FrameSize = lSize;
    pData = pCurrentBits;

    logger.log("C %6d 0x%08x: ResetEvent(hDataParsed)", nFrame, this);
    ResetEvent(hDataParsed);
    logger.log("C %6d 0x%08x: SetEvent(hDataReady)", nFrame, this);
    SetEvent(hDataReady);
    logger.log("C %6d 0x%08x: WaitFor(hDataParsed)", nFrame, this);
    WaitForSingleObject(hDataParsed, INFINITE);
    logger.log("C %6d 0x%08x: Frame Parsed", nFrame, this);

    ++nFrame;

    return S_OK;
Cleanup:
    return hr;
}

HRESULT CSampleGrabber::Pause() {
    return CTransInPlaceFilter::Pause();
}

HRESULT CSampleGrabber::Stop() {
    return CTransInPlaceFilter::Stop();
}

HRESULT CSampleGrabber::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProperties) {
    // Is the input pin connected
    if (m_pInput->IsConnected() == FALSE)
        return E_UNEXPECTED;

    ASSERT(pAlloc);
    ASSERT(pProperties);
    HRESULT hr = NOERROR;

    pProperties->cBuffers = 1;
    pProperties->cbBuffer = m_pInput->CurrentMediaType().GetSampleSize();
    ASSERT(pProperties->cbBuffer);

    // Ask the allocator to reserve us some sample memory, NOTE the function
    // can succeed (that is return NOERROR) but still not have allocated the
    // memory that we requested, so we must check we got whatever we wanted

    ALLOCATOR_PROPERTIES Actual;
    hr = pAlloc->SetProperties(pProperties,&Actual);
    if (FAILED(hr)) return hr;

    ASSERT( Actual.cBuffers >= 1 );

    if (pProperties->cBuffers > Actual.cBuffers || pProperties->cbBuffer > Actual.cbBuffer)
        return E_FAIL;
    return S_OK;
}

HRESULT CSampleGrabber::GetMediaType(int iPosition, CMediaType* pMediaType) {
    // Is the input pin connected
    if (m_pInput->IsConnected() == FALSE)
        return E_UNEXPECTED;

    // This should never happen
    if (iPosition < 0)
        return E_INVALIDARG;

    // Do we have more items to offer
    if (iPosition > 0)
        return VFW_S_NO_MORE_ITEMS;

    *pMediaType = m_pInput->CurrentMediaType();
    return NOERROR;
}

CSampleGrabber* CreateGrabber() {
    HRESULT hr = S_OK;
    CSampleGrabber* obj = new CSampleGrabber(NULL, &hr, FALSE);
    return (SUCCEEDED(hr)) ? obj : NULL;
}
