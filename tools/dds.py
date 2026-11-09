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
names = ["export","import","resource","exception","security","basereloc","debug","arch","globalptr","tls","loadcfg","boundimport","iat","delayimport","clr","reserved"]
for i in range(min(num,16)):
    rva, size = struct.unpack_from("<II", data, dd+i*8)
    if rva: print(f"{names[i]:12s} rva={rva:#x} size={size}")
