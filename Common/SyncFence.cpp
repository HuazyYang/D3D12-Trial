#include "SyncFence.hpp"

HRESULT CreateSyncFence(SyncFence **ppRet) {

  HRESULT hr;
  if (ppRet == NULL)
    V_RETURN(E_INVALIDARG);

  *ppRet = new SyncFence();
  return S_OK;
}

SyncFence::SyncFence() {
  m_pd3dFence = nullptr;
  m_iNextAvailSyncPoint = 0;
  m_hSyncEvent = nullptr;
}

SyncFence::~SyncFence() {
  SAFE_RELEASE(m_pd3dFence);
  if (m_hSyncEvent)
    CloseHandle(m_hSyncEvent);
}

HRESULT SyncFence::Initialize(_In_ ID3D12Device *pd3dDevice) {

  HRESULT hr;

  m_iNextAvailSyncPoint = 0;
  SAFE_RELEASE(m_pd3dFence);
  V_RETURN(pd3dDevice->CreateFence(m_iNextAvailSyncPoint, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&m_pd3dFence)));
  InterlockedIncrement(&m_iNextAvailSyncPoint);

  return hr;
}

HRESULT SyncFence::Signal(_In_ ID3D12CommandQueue *commitQueue,
                          _Out_opt_ UINT64 *pQueuedSyncPoint) {

  HRESULT hr;
  UINT64 nextSyncPoint;

  if (!m_pd3dFence)
    V_RETURN2("SyncFence: fence not initialized!", E_FAIL);
  if (m_iNextAvailSyncPoint == 0) {
    // At this point, we must flush all the sync points.
    DX_TRACE(L"SyncFence: singular point reached, all the sync points will be flushed!");
    FlushAndReset();
  }

  nextSyncPoint = InterlockedIncrement(&m_iNextAvailSyncPoint);
  V_RETURN(commitQueue->Signal(m_pd3dFence, nextSyncPoint));
  if (pQueuedSyncPoint)
    *pQueuedSyncPoint = nextSyncPoint;

  return hr;
}

HRESULT SyncFence::WaitForSyncPoint(_In_ UINT64 iSyncPoint, _In_opt_ INT iMaxSpinCount) {
  HRESULT hr;
  int pending;
  int rc;

  if (!m_pd3dFence)
    V_RETURN2("SyncFence: fence not initialized!", E_FAIL);

  while (--iMaxSpinCount >= 0 && (pending = m_pd3dFence->GetCompletedValue() < iSyncPoint)) {
    if (m_hSyncEvent == NULL) {
      if ((m_hSyncEvent = CreateEventExW(NULL, NULL, 0, EVENT_ALL_ACCESS)) == NULL)
        V_RETURN2("SyncFence: failed to create event!", E_FAIL);
    }
  }

  if (pending) {
    m_pd3dFence->SetEventOnCompletion(iSyncPoint, m_hSyncEvent);

    rc = WaitForSingleObject(m_hSyncEvent, INFINITE);
    if (rc == WAIT_FAILED) {
      V_RETURN2("SyncFence: wait failed!", E_FAIL);
    } else if (rc == WAIT_TIMEOUT) {
      V_RETURN2("SyncFence: awake unexpected!", E_FAIL);
    }
  }

  return S_OK;
}

HRESULT SyncFence::FlushAndReset() {

  HRESULT hr;
  UINT64 nextSyncPoint;

  if (!m_pd3dFence)
    V_RETURN2("SyncFence: fence not initialized!", E_FAIL);

  nextSyncPoint = INT64_MAX; // This will be the maximum synchronizing point we call fetch.
  if (m_iNextAvailSyncPoint != 0)
    // If we overflow INT64, then last synchronizing point have already been set.
    V_RETURN(m_pd3dFence->Signal(nextSyncPoint));

  V_RETURN(WaitForSyncPoint(nextSyncPoint, 4000));

  m_iNextAvailSyncPoint = 1;
  return hr;
}