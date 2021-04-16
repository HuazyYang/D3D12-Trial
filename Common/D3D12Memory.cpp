#include "d3dUtils.h"
#include "D3D12Memory.hpp"
#include <D3D12MemAlloc.h>

AllocationSPtr::AllocationSPtr() {
  m_pPtr = nullptr;
  m_pMemPtr = nullptr;
}

AllocationSPtr::AllocationSPtr(const AllocationSPtr &rhs) {

  SAFE_ADDREF(rhs.m_pPtr); // Thread safe consideration
  m_pPtr = rhs.m_pPtr;
  m_pMemPtr = rhs.m_pMemPtr;
}

AllocationSPtr &AllocationSPtr::operator=(const AllocationSPtr &rhs) {

  *this = nullptr;

  SAFE_ADDREF(rhs.m_pPtr);
  m_pPtr = rhs.m_pPtr;
  m_pMemPtr = rhs.m_pMemPtr;
  return *this;
}

AllocationSPtr &AllocationSPtr::operator=(std::nullptr_t) {

  if (m_pPtr) {
    if (m_pPtr->AddRef() == 3) {
      m_pPtr->Release();
      m_pPtr->Release();
      m_pMemPtr->Release();
    } else {
      m_pPtr->Release();
      m_pPtr->Release();
    }
    m_pPtr = nullptr;
    m_pMemPtr = nullptr;
  }
  return *this;
}

ID3D12Resource *AllocationSPtr::Get() const {
  return m_pPtr;
}


ID3D12Resource* AllocationSPtr::operator->() {
  return m_pPtr;
}

AllocationSPtr::operator ID3D12Resource*() {
  return m_pPtr;
}

AllocationSPtr::~AllocationSPtr() {
  *this = nullptr;
}