#pragma once
#include "BNM/BNM.hpp"

using namespace BNM::UNITY_STRUCTS;
using namespace BNM::MONO_STRUCTS;
using namespace BNM;

namespace Unity
{
    namespace Screen
    {
        static BNM::Class Screen;
        static BNM::Property<int> Height;
        static BNM::Property<int> Width;
        static bool is_done = false;

        void Setup()
        {
            Screen = BNM::Class("UnityEngine", "Screen");
            Height = Screen.GetPropertyByName("height");
            Width = Screen.GetPropertyByName("width");
            is_done = true;
        }
    }
}