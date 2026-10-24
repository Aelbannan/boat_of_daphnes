// Test control (testing only): guest memory written and read at given
// retraces, so a scripted run can set up a situation (an item in hand, a flag,
// health) and report what the game did, without a save file made by hand.
// scripts/windows/scenario.py builds these lists from named fields.
//
//   BLUEWAKE_TEST_POKE=retrace:address=value[/size][,...]
//       writes value (size 1, 2 or 4 bytes; default 1) at that retrace;
//       retrace+ (e.g. 20400+:...) writes it every retrace from then on.
//   BLUEWAKE_TEST_PEEK=retrace:address[/size][,...]
//       logs "[test] peek retrace=R address=A size=S value=V".
#ifndef BLUEWAKE_TEST_CONTROL_H
#define BLUEWAKE_TEST_CONTROL_H

#include "core/cpu.h"

void bluewake_test_control_attach(void);
void bluewake_test_control_retrace(CPUState* cpu, u64 retrace);

#endif
