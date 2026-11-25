#pragma once

#include "demeter_core.h"

void halBegin();
void halPoll(const demeter::Controller& controller);
demeter::Readings halReadings(const demeter::Controller& controller);
void halApplyOutputs(demeter::Controller& controller);

void shellyBegin();
void shellyPoll(demeter::Controller& controller);
bool shellyPumpReachable();
bool shellyLightsReachable();
bool shellyPumpOn();
bool shellyLightsOn();

void netBegin(demeter::Controller& controller);
void netPoll(demeter::Controller& controller);
