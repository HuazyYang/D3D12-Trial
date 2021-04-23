#pragma once
#include "d3dUtils.h"

class SyncFence: public Unknown12 {

friend HRESULT CreateSyncFence(SyncFence **ppRet);

public:

  operator bool() const;
  bool operator == (nullptr_t) const;
  bool operator != (nullptr_t) const;

  HRESULT Initialize(_In_ ID3D12Device *pd3dDevice);

  HRESULT Signal(_In_ ID3D12CommandQueue *commitQueue, _Out_opt_ UINT64 *pQueuedSyncPoint);

  HRESULT WaitForSyncPoint(_In_ UINT64 iSyncPoint, _In_opt_ INT iMaxSpinCount = 4000);

  HRESULT FlushAndReset();

private:
  SyncFence();
  virtual ~SyncFence();

  ID3D12Fence *m_pd3dFence;
  UINT64 m_iNextAvailSyncPoint;
  HANDLE m_hSyncEvent;
};

inline SyncFence::operator bool() const {
  return m_pd3dFence != nullptr;
}

inline bool SyncFence::operator == (nullptr_t) const {
  return m_pd3dFence == nullptr;
}

inline bool SyncFence::operator!=(nullptr_t) const {
  return m_pd3dFence != nullptr;
}