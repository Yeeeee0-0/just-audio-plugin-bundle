#!/usr/bin/env python3
"""Check the literal C++ parameter table against the reviewed first-release map."""
import json,re
from pathlib import Path
root=Path(__file__).resolve().parent
rows=json.loads((root/'parameter-map.json').read_text())['parameters']
manifest=json.loads((root/'manifest.json').read_text())
source=(root/'Parameters.hpp').read_text()
pattern=r'\{"([^\"]+)",(\d+),"([^\"]+)","([^\"]*)",([^,]+),([^,]+),([^,]+),Mapping::(\w+),(\d+),true,"all",Transition::(\w+),([^,}]+)(?:,(\w+))?\}'
parsed=re.findall(pattern,source)
arrays={name:json.loads('['+labels+']') for name,labels in re.findall(r'const char\* (\w+)\[\]=\{([^}]+)\}',source)}
golden={'bypass':['Off','On'],'enabled':['Off','On'],'type':['Bell','Low Shelf','High Shelf','High Pass','Low Pass','Notch'],'target':['Stereo','Mid','Side'],'slope':['6','12','24','36','48'],'dynamic_enabled':['Off','On'],'detector':['Peak','RMS'],'source':['Internal','External']}
assert len(parsed)==195 and len(rows)==183
extensions=json.loads((root/"slope-extension-map.json").read_text())["parameters"]
for b,(p,row) in enumerate(zip(parsed[183:],extensions)):
    key,id,title,unit,lo,hi,init,mapping,steps,transition,ms,label_array=p
    assert key==row["stableKey"]==f"band{b+1:02}.slope_extension"
    assert int(id)==row["paramID"]==115+32*b
    assert (unit,float(lo),float(hi),float(init),mapping,int(steps),transition,float(ms))==("",0,3,0,"linear",3,"discrete",0)
    assert arrays[label_array]==["Legacy","18 dB/oct","72 dB/oct","96 dB/oct"]
assert len({int(p[1]) for p in parsed})==195
assert manifest['parameterSpecs'][0]==rows[0]
for row,p in zip(rows,parsed):
    key,id,title,unit,lo,hi,init,mapping,steps,transition,ms,label_array=p
    assert (key,int(id),title,unit,mapping,int(steps))==(row['stableKey'],row['paramID'],row['title'],row['unit'],row['mapping'],row['stepCount'])
    assert (float(lo),float(hi),float(init),float(ms))==(row['min'],row['max'],row['default'],row['smoothingMs'])
    assert transition==('discrete' if row['stepCount'] else 'continuous')
    field=key.split('.')[-1]
    if row['stepCount']:
        labels=golden[field]
        assert arrays[label_array]==labels
        assert len(row['enumValues'])==row['stepCount']+1
        for ordinal,value in enumerate(row['enumValues']):
            assert value['ordinal']==ordinal and value['normalized']==ordinal/row['stepCount'] and value['label']==labels[ordinal]
            if field=='slope':assert value['dBPerOctave']==[6,12,24,36,48][ordinal]
    else:assert not label_array and 'enumValues' not in row
    policy=row['smoothing']
    if field=='bypass':assert policy['mode']=='linearRamp' and policy['durationMs']==float(ms)==5
    elif not row['stepCount']:
        assert policy['mode']=='onePole' and policy['timeConstantMs']==float(ms)==10
        assert policy['domain']==('logPhysical' if field in ['frequency','q','attack','release'] else 'physical')
    elif field in ['enabled','type','target','slope']:assert policy['mode']=='dualPathCrossfade' and policy['durationMs']==5
    else:assert policy['mode']=='discrete'
assert rows[0]['stableKey']=='eq.bypass' and rows[0]['paramID']==0
assert len({r['paramID'] for r in rows})==len(rows)
assert len({r['stableKey'] for r in rows})==len(rows)
assert 'enum class Target : int { stereo=0, mid=1, side=2 }' in source
for b in range(12):
    assert rows[3+b*15+2]['stableKey']==f'band{b+1:02}.target'
    assert rows[3+b*15+2]['paramID']==102+32*b
print('PASS 12 append-only slope extensions and 183 literal parameters, every enum ordinal/label/value, smoothing policies and bypass compatibility; independent C++ enum golden test also required')
