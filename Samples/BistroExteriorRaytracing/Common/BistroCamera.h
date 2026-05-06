#pragma once

#include <DirectXMath.h>
#include <Windows.h>

namespace Bistro
{
    class FpsCamera
    {
    public:
        void Reset(const DirectX::XMFLOAT3& position, float yawRadians, float pitchRadians);
        void SetActive(bool active);
        void OnMouseButton(UINT message, WPARAM wParam);
        void OnMouseMove(LPARAM lParam);
        void Update(float deltaSeconds);
        void SetMoveSpeeds(float baseSpeed, float fastSpeed);
        float GetBaseMoveSpeed() const;
        float GetFastMoveSpeed() const;
        float GetYawRadians() const;
        float GetPitchRadians() const;

        DirectX::XMMATRIX GetViewMatrix() const;
        DirectX::XMFLOAT3 GetPosition() const;

    private:
        DirectX::XMFLOAT3 m_position = DirectX::XMFLOAT3(0.0f, 3.0f, -10.0f);
        float m_yaw = 0.0f;
        float m_pitch = 0.0f;
        float m_baseMoveSpeed = 5.0f;
        float m_fastMoveSpeed = 18.0f;
        bool m_active = true;
        bool m_lookActive = false;
        POINT m_lastMouse = {};
    };
}

