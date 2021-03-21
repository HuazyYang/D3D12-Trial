#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
  XMMATRIX I = XMMatrixIdentity();
  XMStoreFloat4x4(&mView, I);
  XMStoreFloat4x4(&mProj, I);
  XMStoreFloat4x4(&m_ViewProjCache, I);
  m_fFocusDistance = .0f;

  SetLens(0.25f*XM_PI, 1.0f, 1.0f, 1000.0f);
}

void Camera::SetViewParams(const DirectX::XMFLOAT3 &vEyePosW, const DirectX::XMFLOAT3 &vTargetPos) {
  mPosition = vEyePosW;
  XMStoreFloat3(&mLook, XMVector3Normalize(XMLoadFloat3(&vTargetPos) - XMLoadFloat3(&vEyePosW)));

  mViewDirty = true;
  UpdateViewMatrix();
}
void Camera::SetOrbit(float fRadius, float fTheta, float fPhi) {
  XMVECTOR pos = XMVectorSet(fRadius*sin(fPhi)*cos(fTheta), fRadius*cos(fPhi), fRadius*sin(fPhi)*sin(fTheta), 1.0f);
  XMStoreFloat3(&mPosition, pos);
  XMStoreFloat3(&mLook, XMVector3Normalize(-pos));

  mViewDirty = true;
  UpdateViewMatrix();
}

void Camera::SetProjMatrix(float fFovY, float fAspect, float fNear, float fFar) {
  SetLens(fFovY, fAspect, fNear, fFar);
}

void Camera::Rotate(float dx, float dy) {

  XMMATRIX V = XMLoadFloat4x4(&mView);
  XMMATRIX Vrot = V;
  XMMATRIX Vtrans = XMMatrixIdentity();

  Vrot.r[3] = XMVectorSet(.0f, .0f, .0f, 1.0f);
  Vtrans.r[3] = V.r[3];

  V = Vrot * XMMatrixRotationX(dy) * XMMatrixRotationY(dx) * Vtrans;

  XMMATRIX invV = XMMatrixInverse(nullptr, V);

  XMVECTOR vEyePos = XMVector4Transform(XMVectorSet(.0f, .0f, .0f, 1.0f), invV);
  XMVECTOR vRight = XMVector3Transform(XMVectorSet(1.0f, .0f, 0.0f, .0f), invV);

  XMStoreFloat3(&mPosition, vEyePos);
  XMStoreFloat3(&mLook, XMVector3Normalize(-vEyePos));
  XMStoreFloat3(&mRight, vRight);

  mViewDirty = true;
  UpdateViewMatrix();
}
void Camera::Scale(float dz, float fMinRadius, float fMaxRadius) {

  float fRadius = XMVectorGetX(XMVector3Length(XMLoadFloat3(&mPosition)));
  float fRadius1 = fRadius;

  fRadius1 += dz;
  fRadius1 = std::max(fRadius1, fMinRadius);
  fRadius1 = std::min(fRadius1, fMaxRadius);

  XMStoreFloat3(&mPosition, XMVectorScale(XMLoadFloat3(&mPosition), fRadius1 / fRadius));

  mViewDirty = true;
  UpdateViewMatrix();
}

XMVECTOR Camera::GetPosition()const
{
  return XMLoadFloat3(&mPosition);
}

XMFLOAT3 Camera::GetPosition3f()const
{
  return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
  mPosition = XMFLOAT3(x, y, z);
  mViewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& v)
{
  mPosition = v;
  mViewDirty = true;
}

XMVECTOR Camera::GetRight()const
{
  return XMLoadFloat3(&mRight);
}

XMFLOAT3 Camera::GetRight3f()const
{
  return mRight;
}

XMVECTOR Camera::GetUp()const
{
  return XMLoadFloat3(&mUp);
}

XMFLOAT3 Camera::GetUp3f()const
{
  return mUp;
}

XMVECTOR Camera::GetLook()const
{
  return XMLoadFloat3(&mLook);
}

XMFLOAT3 Camera::GetLook3f()const
{
  return mLook;
}

float Camera::GetNearZ()const
{
  return mNearZ;
}

float Camera::GetFarZ()const
{
  return mFarZ;
}

float Camera::GetAspect()const
{
  return mAspect;
}

float Camera::GetFovY()const
{
  return mFovY;
}

float Camera::GetFovX()const
{
  float halfWidth = 0.5f*GetNearWindowWidth();
  return 2.0f*atan(halfWidth / mNearZ);
}

float Camera::GetNearWindowWidth()const
{
  return mAspect * mNearWindowHeight;
}

float Camera::GetNearWindowHeight()const
{
  return mNearWindowHeight;
}

float Camera::GetFarWindowWidth()const
{
  return mAspect * mFarWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
  return mFarWindowHeight;
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
  // cache properties
  mFovY = fovY;
  mAspect = aspect;
  mNearZ = zn;
  mFarZ = zf;

  mNearWindowHeight = 2.0f * mNearZ * tanf(0.5f*mFovY);
  mFarWindowHeight = 2.0f * mFarZ * tanf(0.5f*mFovY);

  XMMATRIX P = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
  XMStoreFloat4x4(&mProj, P);

  XMMATRIX VP = XMLoadFloat4x4(&mView) * XMLoadFloat4x4(&mProj);
  XMStoreFloat4x4(&m_ViewProjCache, VP);
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
  XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
  XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
  XMVECTOR U = XMVector3Cross(L, R);

  XMStoreFloat3(&mPosition, pos);
  XMStoreFloat3(&mLook, L);
  XMStoreFloat3(&mRight, R);
  XMStoreFloat3(&mUp, U);

  mViewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
  XMVECTOR P = XMLoadFloat3(&pos);
  XMVECTOR T = XMLoadFloat3(&target);
  XMVECTOR U = XMLoadFloat3(&up);

  LookAt(P, T, U);

  mViewDirty = true;
}

XMMATRIX Camera::GetView()const
{
  assert(!mViewDirty);
  return XMLoadFloat4x4(&mView);
}

XMMATRIX Camera::GetProj()const
{
  return XMLoadFloat4x4(&mProj);
}

DirectX::XMMATRIX Camera::GetViewProj() const {
  return XMLoadFloat4x4(&m_ViewProjCache);
}
const DirectX::XMFLOAT3 Camera::GetEyePosW() const {
  return mPosition;
}

XMFLOAT4X4 Camera::GetView4x4f()const
{
  assert(!mViewDirty);
  return mView;
}

XMFLOAT4X4 Camera::GetProj4x4f()const
{
  return mProj;
}

void Camera::Strafe(float d)
{
  // mPosition += d*mRight
  XMVECTOR s = XMVectorReplicate(d);
  XMVECTOR r = XMLoadFloat3(&mRight);
  XMVECTOR p = XMLoadFloat3(&mPosition);
  XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, r, p));

  mViewDirty = true;
}

void Camera::Walk(float d)
{
  // mPosition += d*mLook
  XMVECTOR s = XMVectorReplicate(d);
  XMVECTOR l = XMLoadFloat3(&mLook);
  XMVECTOR p = XMLoadFloat3(&mPosition);
  XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, l, p));

  mViewDirty = true;
}

void Camera::Pitch(float angle)
{
  // Rotate up and look vector about the right vector.

  XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);

  XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
  XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

  mViewDirty = true;
}

void Camera::RotateY(float angle)
{
  // Rotate the basis vectors about the world y-axis.

  XMMATRIX R = XMMatrixRotationY(angle);

  XMStoreFloat3(&mRight, XMVector3TransformNormal(XMLoadFloat3(&mRight), R));
  XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
  XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

  mViewDirty = true;
}

void Camera::UpdateViewMatrix()
{
  if (mViewDirty) {
    XMVECTOR R = XMLoadFloat3(&mRight);
    XMVECTOR U = XMLoadFloat3(&mUp);
    XMVECTOR L = XMLoadFloat3(&mLook);
    XMVECTOR P = XMLoadFloat3(&mPosition);

    // Keep camera's axes orthogonal to each other and of unit length.
    L = XMVector3Normalize(L);
    U = XMVector3Normalize(XMVector3Cross(L, R));

    // U, L already ortho-normal, so no need to normalize cross product.
    R = XMVector3Cross(U, L);

    // Fill in the view matrix entries.
    float x = -XMVectorGetX(XMVector3Dot(P, R));
    float y = -XMVectorGetX(XMVector3Dot(P, U));
    float z = -XMVectorGetX(XMVector3Dot(P, L));

    XMStoreFloat3(&mRight, R);
    XMStoreFloat3(&mUp, U);
    XMStoreFloat3(&mLook, L);

    mView(0, 0) = mRight.x;
    mView(1, 0) = mRight.y;
    mView(2, 0) = mRight.z;
    mView(3, 0) = x;

    mView(0, 1) = mUp.x;
    mView(1, 1) = mUp.y;
    mView(2, 1) = mUp.z;
    mView(3, 1) = y;

    mView(0, 2) = mLook.x;
    mView(1, 2) = mLook.y;
    mView(2, 2) = mLook.z;
    mView(3, 2) = z;

    mView(0, 3) = 0.0f;
    mView(1, 3) = 0.0f;
    mView(2, 3) = 0.0f;
    mView(3, 3) = 1.0f;

    XMMATRIX VP = XMLoadFloat4x4(&mView) * XMLoadFloat4x4(&mProj);
    XMStoreFloat4x4(&m_ViewProjCache, VP);

    mViewDirty = false;
  }
}
