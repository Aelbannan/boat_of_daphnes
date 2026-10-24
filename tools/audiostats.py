# Dev helper: what a raw audio dump (SA2_AUDIO_DUMP: 32 kHz 16-bit stereo in host order, as the
# host plays it) holds - per second, the peak and RMS level of each channel - and a WAV copy to listen to.
#   python tools/audiostats.py <dump.raw> [out.wav]
import array
import math
import sys
import wave

path = sys.argv[1]
data = array.array('h', open(path, 'rb').read())
rate = 32000
print(f'{len(data) // 2 / rate:.1f} s')
for sec in range(len(data) // 2 // rate):
    chunk = data[sec * rate * 2:(sec + 1) * rate * 2]
    l, r = chunk[0::2], chunk[1::2]
    def stats(ch):
        peak = max(abs(v) for v in ch) if ch else 0
        rms = math.sqrt(sum(v * v for v in ch) / len(ch)) if ch else 0
        return peak, rms
    (pl, rl), (pr, rr) = stats(l), stats(r)
    print(f'{sec:4d}s  L peak {pl:6d} rms {rl:8.1f}   R peak {pr:6d} rms {rr:8.1f}')
if len(sys.argv) > 2:
    out = wave.open(sys.argv[2], 'wb')
    out.setnchannels(2)
    out.setsampwidth(2)
    out.setframerate(rate)
    out.writeframes(data.tobytes())
    out.close()
    print('wrote', sys.argv[2])
