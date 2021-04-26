#include "d3dUtils.h"
#include "D3D12MemAllocator.hpp"

D3D12MAResourceSPtr::D3D12MAResourceSPtr() {
  m_pPtr = nullptr;
  m_pMemPtr = nullptr;
}

D3D12MAResourceSPtr::D3D12MAResourceSPtr(D3D12MAResourceSPtr &&rhs)
: D3D12MAResourceSPtr() {
  std::swap(m_pPtr, rhs.m_pPtr);
  std::swap(m_pMemPtr, rhs.m_pMemPtr);
}

D3D12MAResourceSPtr::D3D12MAResourceSPtr(const D3D12MAResourceSPtr &rhs) {

  SAFE_ADDREF(rhs.m_pPtr); // Thread safe consideration
  m_pPtr = rhs.m_pPtr;
  m_pMemPtr = rhs.m_pMemPtr;
}

D3D12MAResourceSPtr &D3D12MAResourceSPtr::operator = (D3D12MAResourceSPtr && rhs) {
  std::swap(m_pPtr, rhs.m_pPtr);
  std::swap(m_pMemPtr, rhs.m_pMemPtr);
  rhs = nullptr;
  return *this;
}

D3D12MAResourceSPtr &D3D12MAResourceSPtr::operator=(const D3D12MAResourceSPtr &rhs) {

  *this = nullptr;

  SAFE_ADDREF(rhs.m_pPtr);
  m_pPtr = rhs.m_pPtr;
  m_pMemPtr = rhs.m_pMemPtr;
  return *this;
}

D3D12MAResourceSPtr &D3D12MAResourceSPtr::operator=(std::nullptr_t) {

  if (m_pPtr) {
    if (m_pPtr->AddRef() == ((m_pMemPtr != nullptr) + 2)) {
      m_pPtr->Release();
      m_pPtr->Release();
      m_pMemPtr ? m_pMemPtr->Release() : nullptr;
    } else {
      m_pPtr->Release();
      m_pPtr->Release();
    }
    m_pPtr = nullptr;
    m_pMemPtr = nullptr;
  }
  return *this;
}

ID3D12Resource *D3D12MAResourceSPtr::Get() const {
  return m_pPtr;
}

bool D3D12MAResourceSPtr::operator==(const D3D12MAResourceSPtr &rhs) const {
  return m_pPtr == rhs.m_pPtr;
}

bool D3D12MAResourceSPtr::operator!=(const D3D12MAResourceSPtr &rhs) const {
  return m_pPtr != rhs.m_pPtr;
}

D3D12MAResourceSPtr::operator bool() const {
  return m_pPtr != nullptr;
}

ID3D12Resource* D3D12MAResourceSPtr::operator->() const {
  return m_pPtr;
}

D3D12MAResourceSPtr::~D3D12MAResourceSPtr() {
  *this = nullptr;
}

D3D12MAAllocator::D3D12MAAllocator() {
  m_pAllocator = nullptr;
}

D3D12MAAllocator::~D3D12MAAllocator() {
  if(m_pAllocator) {
    m_pAllocator->Release();
  }
}

HRESULT D3D12MAAllocator::Initialize(const D3D12MA_ALLOCATOR_DESC *pDesc) {

  if(m_pAllocator) m_pAllocator->Release();

  return D3D12MA::CreateAllocator(pDesc, &m_pAllocator);
}