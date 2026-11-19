#pragma once

#include <vector>
#include <string>

namespace displays {
    std::vector<std::string> menu {
        "Calibrate rotor",
        "Rotation speed",
        "Start shooting",
    };
    std::vector<std::string> calibration {
        "Align the gun!", 
        "Press to exit!",
    };
    std::vector<std::string> rotation {
        "", 
        "Press to exit!",
    };
    std::vector<std::string> shoot {
        "",
        "Press to exit!",
    };

    std::string empty_line {"                "};
} //namespace menu_displays

/*
    Joystick pins
*/
#define JOY_X 15
#define JOY_Y 7
#define JOY_SW 6

/*
    Stepper pins
*/
#define STEPPER_IN1 19
#define STEPPER_IN2 18
#define STEPPER_IN3 5
#define STEPPER_IN4 17

/*
    Ultrasound pins
*/
#define ECO 10
#define TRIGGER 9

/*
    Buzzer pins
*/
#define BUZZER 13

/*
    LCD pins
*/
#define RS 35
#define EN 36
#define D4 37
#define D5 38
#define D6 39
#define D7 40

#define UP -1
#define DOWN 1
#define LEFT -1
#define RIGHT 1