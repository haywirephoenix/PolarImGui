//
// Created by letha on 9/4/2021.
// Updated by Tomie on 3/27/2023
//

#ifndef IMGUIANDROID_FUNCTIONPOINTERS_H
#define IMGUIANDROID_FUNCTIONPOINTERS_H

#include "BNM/BNM.hpp"

using namespace BNM::UnityEngine;
using namespace BNM::Structures::Unity;
using namespace BNM;

namespace Pointers {

    BNM::Class GameObject;
    BNM::Property<void *> GameObject_Transform;

    BNM::Class Transform;
    BNM::Property<Vector3> Transform_Position;

    BNM::Class Camera;
    BNM::Method<Vector3> Camera_WorldToScreenPoint;
    BNM::Property<void *> Camera_Main;

    void LoadPointers() {
        GameObject = BNM::Class("UnityEngine", "GameObject");
        GameObject_Transform = GameObject.GetProperty("transform");

        Transform = BNM::Class("UnityEngine", "Transform");
        Transform_Position   = Transform.GetProperty("position");

        Camera = BNM::Class("UnityEngine", "Camera");
        Camera_WorldToScreenPoint = Camera.GetMethod("WorldToScreenPoint", 1);
        Camera_Main          = Camera.GetProperty("main");
    }
}

#endif // IMGUIANDROID_FUNCTIONPOINTERS_H