#ifndef BLUEWAKE_AUDIO_VOLUME_H
#define BLUEWAKE_AUDIO_VOLUME_H

#include "core/cpu.h"

// Music, Sound Effects and Fanfares volume (Settings > Audio), made inside the game's own audio (JAudio), not in the mix.
//
// - Sound effects: JAInter::SeMgr::seCategoryVolume (a pointer at 0x803F7608 to 8 floats, one per sound category). The game
//   multiplies every playing effect's volume by its category's value and rewrites the eight at scene changes, dialogue and
//   pausing (JAIZelBasic::setSeCategoryVolume). The host keeps the game's own value as the base and writes base * scale.
// - Music and fanfares: JAInter::SequenceMgr::seqTrackInfo (a pointer at 0x803F7620 to 3 SeqUpdateData, 0x50 bytes each). A
//   sequence's volume is the product of its JAISound's mVolumes[0..14] (SeqParameter + 0x110, MoveParaSet 0x10 bytes); line 12
//   is one nothing in the game uses, so the scale sits there and the update flag (SeqUpdateData::mActiveTrackFlag bit 0x40000)
//   makes the game send the new product to the tracks. The sound-effect sequence (id 0x80000800) is left alone. The item
//   fanfares and jingles (item get, heart, song, pearl, chest, game over, race end) take the Fanfares scale, the rest of the
//   sequences the Music scale.
// Streams (the cutscene music of JAIStreamMgr) are not touched.
void bluewake_audio_volume_set(int music_percent, int sfx_percent, int fanfare_percent); // any thread, 0 to 100
void bluewake_audio_volume_retrace(CPUState* cpu); // the game thread, every retrace
int bluewake_audio_volume_sfx_percent(void); // the Sound Effects setting, 0 to 100 (for sounds the host mixes itself)

#endif
