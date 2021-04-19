#pragma once
#include <D3D12MemAlloc.h>

namespace D3D12MA {
class Allocation;
class Allocator;
}; // namespace D3D12MA

typedef D3D12MA::ALLOCATOR_FLAGS D3D12MA_ALLOCATOR_FLAGS;
typedef D3D12MA::ALLOCATOR_DESC D3D12MA_ALLOCATOR_DESC;
typedef D3D12MA::ALLOCATION_DESC D3D12MA_ALLOCATION_DESC;

#define D3D12MA_IID_PPV_ARGS(pAllocation) \
  (pAllocation)->GetAllocationAddressOf(), IID_PPV_ARGS((pAllocation)->ReleaseAndGetAddressOf())

class D3D12MemAllocationSPtr {
public:
  D3D12MemAllocationSPtr();
  D3D12MemAllocationSPtr(const D3D12MemAllocationSPtr &);
  D3D12MemAllocationSPtr(D3D12MemAllocationSPtr &&);
  D3D12MemAllocationSPtr &operator=(const D3D12MemAllocationSPtr &);
  D3D12MemAllocationSPtr &operator = (D3D12MemAllocationSPtr&&);
  D3D12MemAllocationSPtr &operator=(std::nullptr_t);
  ~D3D12MemAllocationSPtr();

  bool operator == (const D3D12MemAllocationSPtr &) const;
  bool operator != (const D3D12MemAllocationSPtr &) const;
  operator bool() const;

  ID3D12Resource *Get() const;

  ID3D12Resource *operator->() const;

  D3D12MA::Allocation **GetAllocationAddressOf();
  ID3D12Resource **ReleaseAndGetAddressOf();

private:
  ID3D12Resource *m_pPtr;
  D3D12MA::Allocation *m_pMemPtr;
};

class D3D12MemAllocator {
public:
  D3D12MemAllocator();
  D3D12MemAllocator(const D3D12MemAllocator&) = delete;
  D3D12MemAllocator& operator = (const D3D12MemAllocator &) = delete; 
  ~D3D12MemAllocator();

  HRESULT Initialize(const D3D12MA_ALLOCATOR_DESC *pDesc);

  D3D12MA::Allocator *operator->() const;

  D3D12MA::Allocator *Get() const;

private:
  D3D12MA::Allocator *m_pAllocator;
};

//
// inline implementation
//
inline D3D12MA::Allocation **D3D12MemAllocationSPtr::GetAllocationAddressOf() {
  return &m_pMemPtr;
}

inline ID3D12Resource **D3D12MemAllocationSPtr::ReleaseAndGetAddressOf() {
  *this = nullptr;
  return &m_pPtr;
}

inline D3D12MA::Allocator *D3D12MemAllocator::operator->() const {
  return m_pAllocator;
}

inline D3D12MA::Allocator *D3D12MemAllocator::Get() const {
  return m_pAllocator;
}