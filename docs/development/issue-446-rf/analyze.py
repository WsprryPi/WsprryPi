import json, pathlib, sys
import numpy as np
root=pathlib.Path(sys.argv[1]); freq=int(sys.argv[2]) if len(sys.argv)>2 else int(root.name); rate=250000
iq=np.memmap(root/'capture.cf32',dtype='<c8',mode='r')
window=np.hanning(rate); nfft=2097152
axis=np.fft.fftfreq(nfft,1/rate)
def feature(power, center, width):
    idx=np.flatnonzero(np.abs(axis-center)<width)
    k=idx[np.argmax(power[idx])]
    y=np.log(np.maximum(power[k-1:k+2],1e-100))
    shift=.5*(y[0]-y[2])/(y[0]-2*y[1]+y[2])
    return {'hz':float((k+shift)*rate/nfft), 'power':float(power[k]), 'contrast_db':float(10*np.log10(power[k]/np.median(power[idx])))}
rows=[]
for i in range(len(iq)//rate):
    p=np.abs(np.fft.fft(np.asarray(iq[i*rate:(i+1)*rate])*window,nfft))**2
    rows.append({'second':i, 'tone':feature(p,25000,600),'low_ref':feature(p,35000,100),'high_ref':feature(p,45000,100)})
(root/'frequency-windows.json').write_text(json.dumps(rows,indent=2))
print(json.dumps([{'s':r['second'],'tone':round(r['tone']['hz']-25000,4),'power':round(10*np.log10(r['tone']['power']),1),'ref':round(r['low_ref']['hz']-35000,4),'high_power':round(10*np.log10(r['high_ref']['power']),1)} for r in rows],indent=2))
