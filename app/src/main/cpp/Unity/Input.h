#pragma once

#include "BNM/BNM.hpp"
#include "Screen.h"

using namespace BNM::UnityEngine;
using namespace BNM;

namespace Unity
{
    namespace Input
    {
        enum TouchPhase {
            Began,
            Moved,
            Stationary,
            Ended,
            Canceled
        };

        enum TouchType {
            Direct,
            Indirect,
            Stylus
        };

        struct Touch {
            int m_FingerId{};
            Vector2 m_Position;
            Vector2 m_RawPosition;
            Vector2 m_PositionDelta;
            float m_TimeDelta{};
            int m_TapCount{};
            TouchPhase m_Phase{};
            TouchType m_Type{};
            float m_Pressure{};
            float m_maximumPossiblePressure{};
            float m_Radius{};
            float m_RadiusVariance{};
            float m_AltitudeAngle{};
            float m_AzimuthAngle{};
        };

        static BNM::Class Input;
        static BNM::Method<Touch> GetTouch;
        static BNM::Method<bool> GetMouseButtonDown;
        static bool is_done = false;

        Touch (*old_FakeGetTouch)(int index);

        Touch FakeGetTouch(int index)
        {
            Touch _touch = old_FakeGetTouch(index);

            if (!init)
                return _touch;

            if (index == 0)
            {
                ImGuiIO &io = ImGui::GetIO();
                float x = _touch.m_Position.x;
                float y = static_cast<float>(std::round(Unity::Screen::Height.Get())) - _touch.m_Position.y;

                if (_touch.m_Phase == TouchPhase::Began)
                {
                    io.AddMousePosEvent(x, y);
                    io.AddMouseButtonEvent(0, GetMouseButtonDown(0));
                }
                if (_touch.m_Phase == TouchPhase::Moved)
                {
                    io.AddMousePosEvent(x, y);
                }
                if (_touch.m_Phase == TouchPhase::Ended)
                {
                    io.AddMouseButtonEvent(0, GetMouseButtonDown(0));
                    io.AddMouseButtonEvent(1, GetMouseButtonDown(1));
                    io.AddMouseButtonEvent(2, GetMouseButtonDown(3));
                }

                if (io.WantCaptureMouse)
                    return old_FakeGetTouch(-1);
            }

            return _touch;
        }

        void Setup()
        {
            Input = BNM::Class("UnityEngine", "Input");
            GetTouch     = Input("GetTouch", 1);
            GetMouseButtonDown = Input("GetMouseButtonDown", 1);
            HOOK(GetTouch.GetOffset(), FakeGetTouch, old_FakeGetTouch);
            is_done = true;
        }
    }
}