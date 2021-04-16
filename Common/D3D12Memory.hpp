#pragma once

namespace D3D12MA {
class Allocation;
class Allocator;
}; // namespace D3D12MA

class D3D12AllocationSPtr {
public:
  D3D12AllocationSPtr();
  D3D12AllocationSPtr(const D3D12AllocationSPtr &);
  D3D12AllocationSPtr &operator=(const D3D12AllocationSPtr &);
  D3D12AllocationSPtr &operator=(std::nullptr_t);
  ~D3D12AllocationSPtr();

  ID3D12Resource *Get() const;

  ID3D12Resource *operator->() const;

  operator ID3D12Resource*() const;

private:
  ID3D12Resource *m_pPtr;
  D3D12MA::Allocation *m_pMemPtr;
};