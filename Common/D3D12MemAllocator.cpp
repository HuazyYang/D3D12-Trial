#include "d3dUtils.h"
#include "D3D12MemAllocator.hpp"
#include <D3D12MemAlloc.h>

D3D12MemAllocationSPtr::D3D12MemAllocationSPtr() {
  m_pPtr = nullptr;
  m_pMemPtr = nullptr;
}

D3D12MemAllocationSPtr::D3D12MemAllocationSPtr(D3D12MemAllocationSPtr &&rhs)
: D3D12MemAllocationSPtr() {
  std::swap(m_pPtr, rhs.m_pPtr);
  std::swap(m_pMemPtr, rhs.m_pMemPtr);
}

D3D12MemAllocationSPtr::D3D12MemAllocationSPtr(const D3D12MemAllocationSPtr &rhs) {

  SAFE_ADDREF(rhs.m_pPtr); // Thread safe consideration
  m_pPtr = rhs.m_pPtr;
  m_pMemPtr = rhs.m_pMemPtr;
}

D3D12MemAllocationSPtr &D3D12MemAllocationSPtr::operator = (D3D12MemAllocationSPtr && rhs) {
  std::swap(m_pPtr, rhs.m_pPtr);
  std::swap(m_pMemPtr, rhs.m_pMemPtr);
  return *this;
}

D3D12MemAllocationSPtr &D3D12MemAllocationSPtr::operator=(const D3D12MemAllocationSPtr &rhs) {

  *this = nullptr;

  SAFE_ADDREF(rhs.m_pPtr);
  m_pPtr = rhs.m_pPtr;
  m_pMemPtr = rhs.m_pMemPtr;
  return *this;
}

D3D12MemAllocationSPtr &D3D12MemAllocationSPtr::operator=(std::nullptr_t) {

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

ID3D12Resource *D3D12MemAllocationSPtr::Get() const {
  return m_pPtr;
}

bool D3D12MemAllocationSPtr::operator==(const D3D12MemAllocationSPtr &rhs) const {
  return m_pPtr == rhs.m_pPtr;
}

bool D3D12MemAllocationSPtr::operator!=(const D3D12MemAllocationSPtr &rhs) const {
  return m_pPtr != rhs.m_pPtr;
}

D3D12MemAllocationSPtr::operator bool() const {
  return m_pPtr != nullptr;
}

ID3D12Resource* D3D12MemAllocationSPtr::operator->() const {
  return m_pPtr;
}

D3D12MemAllocationSPtr::~D3D12MemAllocationSPtr() {
  *this = nullptr;
}

D3D12MemAllocator::D3D12MemAllocator() {
  m_pAllocator = nullptr;
}

D3D12MemAllocator::~D3D12MemAllocator() {
  if(m_pAllocator) {
    m_pAllocator->Release();
  }
}

HRESULT D3D12MemAllocator::Initialize(const D3D12MA_ALLOCATOR_DESC *pDesc) {

  if(m_pAllocator) m_pAllocator->Release();

  return D3D12MA::CreateAllocator(pDesc, &m_pAllocator);
}