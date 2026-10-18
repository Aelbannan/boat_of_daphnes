// BR-0018: the guard that lets the Jabun cave door's final cutscene end (see jabun_guard.c).
#pragma once

#include "core/cpu.h"

#include <stdbool.h>

// Once a retrace, with the scene the game is in.
void bluewake_jabun_guard_retrace(CPUState* cpu, const char* stage, int room, unsigned long long retrace);
