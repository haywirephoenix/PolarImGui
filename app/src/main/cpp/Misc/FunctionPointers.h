//
// Created by letha on 9/4/2021.
// Updated by Tomie on 3/27/2023
//

#ifndef IMGUIANDROID_FUNCTIONPOINTERS_H
#define IMGUIANDROID_FUNCTIONPOINTERS_H
using namespace BNM::UNITY_STRUCTS;
namespace Pointers {

    BNM::Class GameObject;
    BNM::Property<void *> GameObject_Transform;

    BNM::Class Transform;
    BNM::Property<Vector3> Transform_Position;

    BNM::Class Camera;
    BNM::Method<Vector3 *> Camera_WorldToScreenPoint;
    BNM::Property<Vector3> Camera_Main;

    void LoadPointers() {
        GameObject = BNM::Class("UnityEngine", "GameObject");
        GameObject_Transform = GameObject.GetPropertyByName("transform");

        Transform = BNM::Class("UnityEngine", "Transform");
        Transform_Position = Transform.GetPropertyByName("position");

        Camera = BNM::Class("UnityEngine", "Camera");
        Camera_WorldToScreenPoint = Camera.GetMethodByName("WorldToScreenPoint", 1);
        Camera_Main = Camera.GetPropertyByName("main");
    }
}
#endif IMGUIANDROID_FUNCTIONPOINTERS_H
