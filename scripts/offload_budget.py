"""Pin/clock budget of one ML-KEM-512 exchange (32 hash calls) on the hash-only chip: run from anywhere."""
import sys; import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'test'))
from hash_jobs import HashJob
import collections
# (kind, name, msg_len, outlen, cbd) ; ML-KEM-512 counts
def job(name,n,out=None,cbd=None,skip=True):
    j=HashJob(name,bytes([1])*n,K=8,skip=skip,burst=True,cbd_eta=cbd,outlen=out)
    w,sq=j.words(); return j,len(w),len(sq)
calls=[]  # (step, name, inlen, out, cbd, count)
def add(step,name,n,out=None,cbd=None,cnt=1): calls.append((step,name,n,out,cbd,cnt))
# keygen
add('G','sha3_512',33,64); add('XOF','shake_128',34,504,cnt=4); add('PRF3','shake_256',33,None,3,4); add('H','sha3_256',800,32)
# encaps
add('H','sha3_256',800,32); add('G','sha3_512',64,64); add('XOF','shake_128',34,504,cnt=4)
add('PRF3','shake_256',33,None,3,2); add('PRF2','shake_256',33,None,2,3)
# decaps
add('G','sha3_512',64,64); add('XOF','shake_128',34,504,cnt=4); add('PRF3','shake_256',33,None,3,2); add('PRF2','shake_256',33,None,2,3); add('J','shake_256',800,32)
agg=collections.OrderedDict()
tot=collections.Counter()
for step,name,n,out,cbd,cnt in calls:
    for skip in (False,True):
        j,w,s=job(name,n,out,cbd,skip)
        a=agg.setdefault(step,collections.Counter())
        a['calls']+= cnt if not skip else 0
        a['clk_noskip' if not skip else 'clk_skip']+=w*cnt
    pad=(n//j.rate+1)*j.rate
    a['in_B']+=n*cnt; a['padded_B']+=pad*cnt; a['out_B']+=j.outlen*cnt
    perms=(n//j.rate+1)+((j.outlen+j.rate-1)//j.rate-1)
    a['perms']+=perms*cnt
print('step calls inB padded outB perms clk_noskip clk_skip')
T=collections.Counter()
for k,a in agg.items():
    print(k,a['calls'],a['in_B'],a['padded_B'],a['out_B'],a['perms'],a['clk_noskip'],a['clk_skip']); T.update(a)
print('TOTAL',dict(T)); print('perm clocks',T['perms']*369, 'ms@50',T['clk_skip']/50e3)
