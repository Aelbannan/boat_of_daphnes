// Settings as named console variables (CVars), the way Ship of Harkinian keeps
// them: dotted names under a few prefixes (gSettings.*, gEnhancements.*,
// gCheats.*, gOpenWindows.*), stored nested under "CVars" in one JSON file
// beside the executable. The C interface is libultraship's CVar bridge
// (CVarGetInteger, CVarSetFloat, ...), so code written against it reads the
// same. The implementation is this fork's (cvar.cpp).
#ifndef BLUEWAKE_SHIP_CVAR_H
#define BLUEWAKE_SHIP_CVAR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CVarColor {
    uint8_t r, g, b, a;
} CVarColor;

// Loads the file (missing is fine: every read returns its default).
void CVarInit(const char* path);
const char* CVarPath(void);

bool CVarExists(const char* name);
// A value of another type is converted (JSON keeps 1.0 as 1).
int32_t CVarGetInteger(const char* name, int32_t defaultValue);
float CVarGetFloat(const char* name, float defaultValue);
// The returned text stays valid until that CVar is set again.
const char* CVarGetString(const char* name, const char* defaultValue);
CVarColor CVarGetColor(const char* name, CVarColor defaultValue);

void CVarSetInteger(const char* name, int32_t value);
void CVarSetFloat(const char* name, float value);
void CVarSetString(const char* name, const char* value);
void CVarSetColor(const char* name, CVarColor value);
// Removes the CVar (reads return their default again), or every CVar whose
// name starts with the prefix.
void CVarClear(const char* name);
void CVarClearBlock(const char* prefix);

// Writes the file now (atomically: a temporary file, then a rename).
void CVarSave(void);
// Marks the file to be written at the next CVarTick (the menu calls this on
// every change; the tick runs once a frame).
void CVarSaveNextFrame(void);
void CVarTick(void);

#ifdef __cplusplus
}
#endif

#endif
