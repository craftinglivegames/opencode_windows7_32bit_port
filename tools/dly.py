import struct
path = r"C:\work\AppData\Roaming\npm\node_modules\opencode-ai\bin\opencode.exe"
data = open(path,"rb").read()
e = struct.unpack_from("<I", data, 0x3C)[0]
coff = e+4
nsec,_,_,_,_,optsz,_ = struct.unpack_from("<HHIIIHH", data, coff)
opt = coff+20
magic = struct.unpack_from("<H", data, opt)[0]
pe32plus = magic==0x20B
dd = opt + (112 if pe32plus else 96)
num = struct.unpack_from("<I", data, opt+(108 if pe32plus else 92))[0]
dd += (num-16)*8
size_of_headers = struct.unpack_from("<I", data, opt+60)[0]
secs=[]
sec_off = opt+optsz
for i in range(nsec):
    b=sec_off+i*40
    vs,va,rs,raw = struct.unpack_from("<IIII", data, b+8)
    secs.append((va,vs,raw))
def r2o(rva):
    for va,vs,raw in secs:
        if va <= rva < va+vs: return rva-va+raw
    return rva if rva < size_of_headers else None
def cstr(off):
    end = data.index(b"\x00", off); return data[off:end].decode("ascii","replace")
dly_rva, dly_size = struct.unpack_from("<II", data, dd+13*8)
off = r2o(dly_rva)
while True:
    attrs, name_rva, hmod, iat_rva, int_rva, bound, unload, ts = struct.unpack_from("<IIIIIIII", data, off)
    if not any((attrs,name_rva,iat_rva,int_rva)): break
    print("DELAY DLL:", cstr(r2o(name_rva)))
    t = int_rva or iat_rva
    toff = r2o(t)
    i=0
    while True:
        val = struct.unpack_from("<Q" if pe32plus else "<I", data, toff+i*(8 if pe32plus else 4))[0]
        if val==0: break
        if val & (1<<(63 if pe32plus else 31)):
            print("   ordinal", val & 0xFFFF)
        else:
            ho = r2o(val & 0x7fffffff)
            print("   ", cstr(ho+2))
        i+=1
    off += 32
