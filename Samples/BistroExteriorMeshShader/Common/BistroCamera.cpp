#include "BistroCamera.h"

#include <algorithm>

using namespace DirectX;

namespace
{
    XMVECTOR CameraForward(float yaw, float pitch)
    {
        return XMVector3Normalize(XMVectorSet(
            sinf(yaw) * cosf(pitch),
            sinf(pitch),
            cosf(yaw) * cosf(pitch),
            0.0f));
    }
}

namespace Bistro
{
    void FpsCamera::Reset(const XMFLOAT3& position, float yawRadians, float pitchRadians)
    {
        m_position = position;
        m_yaw = yawRadians;
        m_pitch = std::clamp(pitchRadians, -1.45f, 1.45f);
    }

    void FpsCamera::SetActive(bool active)
    {
        m_active = active;
        if (!active)
        {
            m_lookActive = false;
        }
    }

    void FpsCamera::OnMouseButton(UINT message, WPARAM)
    {
        if (message == WM_RBUTTONDOWN)
        {
            m_lookActive = true;
            GetCursorPos(&m_lastMouse);
        }
        else if (message == WM_RBUTTONUP)
        {
            m_lookActive = false;
        }
    }

    void FpsCamera::OnMouseMove(LPARAM)
    {
        if (!m_active || !m_lookActive)
        {
            return;
        }

        POINT current{};
        GetCursorPos(&current);
        const float sensitivity = 0.004f;
        m_yaw += static_cast<float>(current.x - m_lastMouse.x) * sensitivity;
        m_pitch += static_cast<float>(current.y - m_lastMouse.y) * sensitivity;
        m_pitch = std::clamp(m_pitch, -1.45f, 1.45f);
        m_lastMouse = current;
    }

    void FpsCamera::Update(float deltaSeconds)
    {
        if (!m_active)
        {
            return;
        }

        float speed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? m_fastMoveSpeed : m_baseMoveSpeed;
        speed *= deltaSeconds;

        XMVECTOR position = XMLoadFloat3(&m_position);
        XMVECTOR forward = CameraForward(m_yaw, 0.0f);
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forward));
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        if (GetAsyncKeyState('W') & 0x8000) position += forward * speed;
        if (GetAsyncKeyState('S') & 0x8000) position -= forward * speed;
        if (GetAsyncKeyState('A') & 0x8000) position -= right * speed;
        if (GetAsyncKeyState('D') & 0x8000) position += right * speed;
        if (GetAsyncKeyState('E') & 0x8000) position += up * speed;
        if (GetAsyncKeyState('Q') & 0x8000) position -= up * speed;

        XMStoreFloat3(&m_position, position);
    }

    void FpsCamera::SetMoveSpeeds(float baseSpeed, float fastSpeed)
    {
        m_baseMoveSpeed = (std::max)(0.1f, baseSpeed);
        m_fastMoveSpeed = (std::max)(m_baseMoveSpeed, fastSpeed);
    }

    float FpsCamera::GetBaseMoveSpeed() const
    {
        return m_baseMoveSpeed;
    }

    float FpsCamera::GetFastMoveSpeed() const
    {
        return m_fastMoveSpeed;
    }

    float FpsCamera::GetYawRadians() const
    {
        return m_yaw;
    }

    float FpsCamera::GetPitchRadians() const
    {
        return m_pitch;
    }

    XMMATRIX FpsCamera::GetViewMatrix() const
    {
        XMVECTOR eye = XMLoadFloat3(&m_position);
        XMVECTOR forward = CameraForward(m_yaw, m_pitch);
        return XMMatrixLookAtLH(eye, eye + forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    }

    XMFLOAT3 FpsCamera::GetPosition() const
    {
        return m_position;
    }
}
