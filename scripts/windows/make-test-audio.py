#!/usr/bin/env python3
"""Generate local deterministic PCM fixtures; never open or alter REAPER."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import wave

parser=argparse.ArgumentParser()
parser.add_argument('new_directory', type=Path)
parser.add_argument('--sample-rate', type=int, choices=[44100,48000,96000], default=48000)
args=parser.parse_args()
out=args.new_directory.resolve()
if out.exists():
    raise SystemExit('Choose a new fixture directory; existing audio is never overwritten.')
out.mkdir(parents=True)
sr=args.sample_rate
frames=sr*12
fixtures={}
for name,channels in [('stereo-tone-sweep-impulse-tail.wav',2),('mono-tone-sweep-impulse-tail.wav',1),('sidechain-pulses.wav',1)]:
    data=bytearray()
    peak=0
    squares=0
    for i in range(frames):
        t=i/sr
        if name.startswith('sidechain'):
            signal=.25*math.sin(2*math.pi*1000*t) if t<9 and (t%1)<.2 else 0.
        elif t<4:
            signal=10**(-18/20)*math.sin(2*math.pi*1000*t)
        elif t<8:
            elapsed=t-4; duration=4; low=20; high=min(20000,sr*.45)
            phase=2*math.pi*low*duration/math.log(high/low)*(math.exp(elapsed*math.log(high/low)/duration)-1)
            signal=10**(-24/20)*math.sin(phase)
        else:
            signal=.25 if i==8*sr else 0.
        samples=[signal] if channels==1 else [signal,signal*.5 if t<4 else -signal*.75]
        for sample in samples:
            value=round(max(-1.,min(1.,sample))*32767)
            data+=struct.pack('<h',value)
            peak=max(peak,abs(value));squares+=value*value
    path=out/name
    with wave.open(str(path),'wb') as wav:
        wav.setnchannels(channels);wav.setsampwidth(2);wav.setframerate(sr);wav.writeframes(data)
    fixtures[name]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'sampleRate':sr,'channels':channels,'frames':frames,'sampleFormat':'signed PCM16 little endian','peakNormalized':peak/32768,'rmsNormalized':math.sqrt(squares/(frames*channels))/32768}
(out/'fixture-manifest.json').write_text(json.dumps({'purpose':'Input fixtures for a NEW REAPER test project; not processed output or a passing DSP result.','timeline':'0-4s 1kHz tone; 4-8s logarithmic sweep; 8s impulse; remaining silence for tail. Sidechain: 200ms pulse every second until 9s.','files':fixtures},indent=2)+'\n',encoding='utf-8')
print('Created only new PCM fixture files:', out)
