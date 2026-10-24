// The runtime's reads and writes of settings go through Boat of Daphnes' CVar store
// (src/bod/runtime/host/src/ship/cvar.cpp: boat-of-daphnes.json beside the program), so the
// runtime's own settings and its menu's are the same values.
#include "runtime/bod.h"

#include "ship/cvar.h"

namespace rt::bod {

int cvarInt(const char* name, int def) { return CVarGetInteger(name, def); }
float cvarFloat(const char* name, float def) { return CVarGetFloat(name, def); }
std::string cvarString(const char* name, const char* def) {
    const char* v = CVarGetString(name, def);
    return v ? v : "";
}
void cvarSetInt(const char* name, int v) {
    CVarSetInteger(name, v);
    CVarSaveNextFrame();
}
void cvarSetFloat(const char* name, float v) {
    CVarSetFloat(name, v);
    CVarSaveNextFrame();
}
void cvarSave() { CVarSave(); }
void cvarSetString(const char* name, const std::string& v) {
    CVarSetString(name, v.c_str());
    CVarSaveNextFrame();
}

}  // namespace rt::bod
