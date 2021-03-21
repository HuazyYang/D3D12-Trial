#pragma once
#include <DirectXMath.h>
#include <algorithm>
#undef min
#undef max

class SimpleOrbitCamera {
public:
   SimpleOrbitCamera() {
       DirectX::XMMATRIX I = DirectX::XMMatrixIdentity();
       m_fRadius = 0.0f;
       m_fTheta = 0.0f;
       m_fPhi = 0.0f;
       DirectX::XMStoreFloat4x4(&m_matView, I);
       DirectX::XMStoreFloat4x4(&m_matProj, I);
       DirectX::XMStoreFloat4x4(&m_matViewProjCache, I);

       m_vTargetPos = { .0f, .0f, .0f };
   }

   void SetViewParams(const DirectX::XMFLOAT3 &vEyePosW, const DirectX::XMFLOAT3 &vTargetPos) {

       DirectX::XMVECTOR vLookAt;
       DirectX::XMVECTOR vSqrt;
       DirectX::XMFLOAT3 v;

       vLookAt = DirectX::XMVectorSubtract(
           DirectX::XMLoadFloat3(&vEyePosW),
           DirectX::XMLoadFloat3(&vTargetPos));

       vSqrt = DirectX::XMVector3Length(vLookAt);
       vLookAt = DirectX::XMVectorDivide(vLookAt, vSqrt);
       DirectX::XMStoreFloat3(&v, vLookAt);

       m_fTheta = atan2f(v.z, v.x);
       m_fPhi = acosf(v.y);

       m_fRadius = DirectX::XMVectorGetX(vSqrt);

       m_vTargetPos = vTargetPos;

       UpdateViewMatrix();
   }

   void SetOrbit(float fRadius, float fTheta, float fPhi) {
       m_fRadius = fRadius;
       m_fTheta = fTheta;
       m_fPhi = fPhi;

       UpdateViewMatrix();
   }


   void SetProjMatrix(float fFovY, float fAspect, float fNear, float fFar) {
       DirectX::XMMATRIX P, V, VP;
       P = DirectX::XMMatrixPerspectiveFovLH(fFovY, fAspect, fNear, fFar);
       V = DirectX::XMLoadFloat4x4(&m_matView);
       VP = V * P;

       DirectX::XMStoreFloat4x4(&m_matProj, P);
       DirectX::XMStoreFloat4x4(&m_matViewProjCache, VP);
   }

   void Rotate(float dx, float dy) {
       // Update angles based on input to orbit camera around box.
       m_fTheta += dx;
       m_fPhi += dy;

       m_fPhi = std::max(m_fPhi, 0.1f);
       m_fPhi = std::min(m_fPhi, DirectX::XM_PI - 0.1f);

       UpdateViewMatrix();
   }

   void Scale(float dz, float fMinRadius, float fMaxRadius) {
       m_fRadius += dz;
       m_fRadius = std::max(m_fRadius, fMinRadius);
       m_fRadius = std::min(m_fRadius, fMaxRadius);

       UpdateViewMatrix();
   }

   const DirectX::XMFLOAT4X4& GetView() const { return m_matView; }
   const DirectX::XMFLOAT4X4& GetProj() const { return m_matProj; }
   const DirectX::XMFLOAT4X4& GetViewProj() const { return m_matViewProjCache; }
   const DirectX::XMFLOAT3 GetEyePosW() const { return m_vEyePosW; }

private:
   void UpdateViewMatrix() {
       DirectX::XMVECTOR vTarget;
       DirectX::XMVECTOR vEyePos;
       DirectX::XMVECTOR vUp;
       float x, y, z;
       DirectX::XMMATRIX matView, matProj, matViewProj;

       vTarget = DirectX::XMLoadFloat3(&m_vTargetPos);

       x = m_fRadius*sin(m_fPhi)*cos(m_fTheta);
       z = m_fRadius*sin(m_fPhi)*sin(m_fTheta);
       y = m_fRadius*cos(m_fPhi);

       vEyePos = DirectX::XMVectorAdd(
           vTarget,
           DirectX::XMVectorSet(x, y, z, 0.0));
       vUp = DirectX::XMVectorSet(0.0, 1.0, 0.0, 0.0);

       /// Update the cache.
       matView = DirectX::XMMatrixLookAtLH(vEyePos, vTarget, vUp);
       matProj = DirectX::XMLoadFloat4x4(&m_matProj);
       matViewProj = matView * matProj;

       DirectX::XMStoreFloat3(&m_vEyePosW, vEyePos);
       DirectX::XMStoreFloat4x4(&m_matView, matView);
       DirectX::XMStoreFloat4x4(&m_matViewProjCache, matViewProj);
   }

   float m_fRadius = 0;
   float m_fTheta = 0;
   float m_fPhi = 0;

   DirectX::XMFLOAT3 m_vTargetPos;
   DirectX::XMFLOAT3 m_vEyePosW;
   DirectX::XMFLOAT4X4 m_matView;
   DirectX::XMFLOAT4X4 m_matProj;
   DirectX::XMFLOAT4X4 m_matViewProjCache;
};

class Camera
{
public:
  Camera();

  void SetViewParams(const DirectX::XMFLOAT3 &vEyePosW, const DirectX::XMFLOAT3 &vTargetPos);
  void SetOrbit(float fRadius, float fTheta, float fPhi);
  void SetProjMatrix(float fFovY, float fAspect, float fNear, float fFar);
  void Rotate(float dx, float dy);
  void Scale(float dz, float fMinRadius, float fMaxRadius);

  // Get/Set world camera position.
  DirectX::XMVECTOR GetPosition()const;
  DirectX::XMFLOAT3 GetPosition3f()const;
  void SetPosition(float x, float y, float z);
  void SetPosition(const DirectX::XMFLOAT3& v);

  // Get camera basis vectors.
  DirectX::XMVECTOR GetRight()const;
  DirectX::XMFLOAT3 GetRight3f()const;
  DirectX::XMVECTOR GetUp()const;
  DirectX::XMFLOAT3 GetUp3f()const;
  DirectX::XMVECTOR GetLook()const;
  DirectX::XMFLOAT3 GetLook3f()const;

  // Get frustum properties.
  float GetNearZ()const;
  float GetFarZ()const;
  float GetAspect()const;
  float GetFovY()const;
  float GetFovX()const;

  // Get near and far plane dimensions in view space coordinates.
  float GetNearWindowWidth()const;
  float GetNearWindowHeight()const;
  float GetFarWindowWidth()const;
  float GetFarWindowHeight()const;

  // Set frustum.
  void SetLens(float fovY, float aspect, float zn, float zf);

  // Define camera space via LookAt parameters.
  void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
  void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

  // Get View/Proj matrices.
  DirectX::XMMATRIX GetView()const;
  DirectX::XMMATRIX GetProj()const;
  DirectX::XMMATRIX GetViewProj() const;

  const DirectX::XMFLOAT3 GetEyePosW() const;
  DirectX::XMFLOAT4X4 GetView4x4f()const;
  DirectX::XMFLOAT4X4 GetProj4x4f()const;

  // Strafe/Walk the camera a distance d.
  void Strafe(float d);
  void Walk(float d);

  // Rotate the camera.
  void Pitch(float angle);
  void RotateY(float angle);

  // After modifying camera position/orientation, call to rebuild the view matrix.
  void UpdateViewMatrix();

private:
  float m_fFocusDistance;

  // Camera coordinate system with coordinates relative to world space.
  DirectX::XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
  DirectX::XMFLOAT3 mRight = { 1.0f, 0.0f, 0.0f };
  DirectX::XMFLOAT3 mUp = { 0.0f, 1.0f, 0.0f };
  DirectX::XMFLOAT3 mLook = { 0.0f, 0.0f, 1.0f };

  // Cache frustum properties.
  float mNearZ = 0.0f;
  float mFarZ = 0.0f;
  float mAspect = 0.0f;
  float mFovY = 0.0f;
  float mNearWindowHeight = 0.0f;
  float mFarWindowHeight = 0.0f;

  bool mViewDirty = true;

  // Cache View/Proj matrices.
  DirectX::XMFLOAT4X4 mView;
  DirectX::XMFLOAT4X4 mProj;
  DirectX::XMFLOAT4X4 m_ViewProjCache;
};

