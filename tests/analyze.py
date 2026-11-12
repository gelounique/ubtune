import sys, numpy as np, wave
def load(p):
    w=wave.open(p); x=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16).astype(float)/32768; return x,w.getframerate()
def yin(fr,fs,fmin=70,fmax=1100):
    W=len(fr)//2; tmax=int(fs/fmin); tmin=int(fs/fmax)
    d=np.array([np.sum((fr[:W]-fr[t:t+W])**2) for t in range(tmax+1)])
    c=np.ones_like(d); cs=np.cumsum(d[1:]); c[1:]=d[1:]*np.arange(1,tmax+1)/np.maximum(cs,1e-12)
    idx=np.where(c[tmin:]<0.15)[0]
    if len(idx)==0: return None
    t=idx[0]+tmin
    while t+1<=tmax and c[t+1]<c[t]: t+=1
    if 1<=t<tmax:
        a,b,cc=c[t-1],c[t],c[t+1]; den=a-2*b+cc; t=t+(0.5*(a-cc)/den if abs(den)>1e-12 else 0)
    return 69+12*np.log2(fs/t/440)
x,fs=load(sys.argv[1]); lat=int(sys.argv[2]) if len(sys.argv)>2 else 0
segs=[(57.3,.6,57),(60.4,.5,60),(62.2,.5,62),(63.6,.6,64),(64.2,.7,64),(66.8,.5,67),(67.3,.6,67),(69.0,.8,69),(45.4,.6,45),(72.6,.6,72)]
t=0; tot=[]
for m,d,tgt in segs:
    a=int((t+0.15)*fs)+lat; b=int((t+d-0.05)*fs)+lat; N=int(0.05*fs)
    ps=[yin(x[i:i+2*N],fs) for i in range(a,b-2*N,N//2)]
    ps=[p for p in ps if p is not None]
    if ps:
        ps=np.array(ps); dev=(ps-tgt)*100
        print(f"sung {m:5.1f} -> target {tgt}: out median {np.median(ps):6.2f}  dev mean {np.mean(dev):+6.1f}c  std {np.std(dev):5.1f}c  frames {len(ps)}")
        tot+=list(dev)
    t+=d+0.05
tot=np.array(tot); print(f"ALL: |dev| mean {np.mean(np.abs(tot)):.1f} cents, 95% < {np.percentile(np.abs(tot),95):.1f} cents")
