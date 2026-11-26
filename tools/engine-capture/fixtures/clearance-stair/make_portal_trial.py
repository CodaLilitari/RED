"""Build three original two-room episodes for the walkthrough clearance comparison."""
import argparse
from pathlib import Path
import re
import struct

from make_project import HERE, resource_archive


def trial(rise, lintel):
    vertices, faces, rooms = [], [], []
    def vertex(x,y,z):
        point = (x,y,z)
        if point not in vertices:
            vertices.append(point)
        return vertices.index(point)
    def ring(y, floor, ceiling):
        return [vertex(x,y,z) for x,z in ((-.25,floor),(.25,floor),(.25,ceiling),(-.25,ceiling))]
    for sector, (a,b,floor,ceil) in enumerate(((-1,0,0,1),(0,1,rise,lintel))):
        front, back = ring(a,floor,ceil), ring(b,floor,ceil)
        portal = ring(0,rise,lintel)
        start = len(faces)
        faces.extend([(5,-1,(0,0,1),[front[0],front[1],back[1],back[0]]),
                      (4,-1,(0,0,-1),[front[3],back[3],back[2],front[2]]),
                      (4,-1,(-1,0,0),[front[1],front[2],back[2],back[1]]),
                      (4,-1,(1,0,0),[front[0],back[0],back[3],front[3]])])
        if sector == 0:
            faces.append((4,-1,(0,1,0),[front[0],front[3],front[2],front[1]]))
            faces.append((0,0,(0,-1,0),portal))
            if rise > 0:
                faces.append((4,-1,(0,-1,0),[back[0],back[1],portal[1],portal[0]]))
            if lintel < ceil:
                faces.append((4,-1,(0,-1,0),[portal[3],portal[2],back[2],back[3]]))
        else:
            faces.extend([(0,1,(0,1,0),[portal[0],portal[3],portal[2],portal[1]]),
                          (4,-1,(0,-1,0),back)])
        ids = sorted({i for _,_,_,indexes in faces[start:] for i in indexes})
        rooms.append(f"SECTOR {sector}\nFLAGS 0\nAMBIENT LIGHT 1\nEXTRA LIGHT 0\nCOLORMAP 0\n"
                     f"TINT 0 0 0\nBOUNDBOX -.25 {a} {floor} .25 {b} {ceil}\n"
                     f"CENTER 0 {(a+b)/2} {(floor+ceil)/2}\nRADIUS 2\nVERTICES {len(ids)}\n" +
                     "".join(f"{n}: {i}\n" for n,i in enumerate(ids)) + f"SURFACES {start} {len(faces)-start}\n")
    def table(name, rows):
        return f"{name} {len(rows)}\n" + "".join(f"{i}: {r}\n" for i,r in enumerate(rows))
    def vector(p):
        return " ".join(map(str,p))
    geo = (table("World vertices",list(map(vector,vertices))) +
           table("World texture vertices",["0 0","64 0","64 64","0 64"]) +
           table("World adjoins",["7 1 0","7 0 0"]) +
           table("World surfaces",[f"{-1 if adj>=0 else 0} 0x{flags:x} 0 {0 if adj>=0 else 4} 3 3 {adj} 0 4 " +
                 " ".join(f"{v},{i}" for i,v in enumerate(ids)) + " 1 1 1 1" for flags,adj,_,ids in faces]) +
           "".join(f"{i}: {vector(normal)}\n" for i,(_,_,normal,_) in enumerate(faces)))
    base = (HERE/"redlift.jkl").read_text(encoding="ascii")
    base = re.sub(r"World vertices .*?\nEND\n", geo+"END\n", base, count=1, flags=re.S)
    base = re.sub(r"World sectors .*?\nEND\n", "World sectors 2\n"+"".join(rooms)+"END\n",base,count=1,flags=re.S)
    base = base.replace("0: rlcontrol.cog 0 46 +rlprobe\n", "0: rltrial.cog\n")
    base = base.replace("0: rlcontrol.cog\n", "0: rltrial.cog\n")
    base = base.replace("rlgate none type=cog move=path collide=3", "rlgate none type=cog move=path collide=0")
    base = (base[:base.index("0: rlgate Gate")] +
            "0: rlgate Observer .2 -.9 .8 0 0 0 0 -1 numframes=1 frame=(.2/-.9/.8:0/0/0)\n"
            "1: rlpilot Player 0 -.45 .09 0 0 0 0 -1\nEND\n")
    return base.replace("\n","\r\n").encode("ascii")


def package(level):
    art = resource_archive()
    count, = struct.unpack_from("<I",art,12)
    entries = {}
    for i in range(count):
        offset,size,name = struct.unpack_from("<II128s",art,16+136*i)
        entries[name.rstrip(b"\0")] = art[offset:offset+size]
    entries[b"jkl\\redlift.jkl"] = level
    entries[b"cog\\rltrial.cog"] = b"symbols\nint status=0 local\nmessage startup\nend\ncode\nstartup:\nreturn;\nend\n"
    entries[b"episode.jk"] = (HERE/"episode.jk").read_bytes()
    header = bytearray(b"GOB " + struct.pack("<III",20,12,len(entries)))
    start = 16 + 136*len(entries)
    for name,data in entries.items():
        header.extend(struct.pack("<II128s",start,len(data),name))
        start += len(data)
    return bytes(header)+b"".join(entries.values())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination",type=Path)
    destination = parser.parse_args().destination
    destination.mkdir(exist_ok=False,parents=True)
    for name,rise,lintel in (("pass",.005,1),("riser",.25,1),("lintel",.005,.075)):
        level = trial(rise,lintel)
        (destination/(name+".jkl")).write_bytes(level)
        (destination/(name+".gob")).write_bytes(package(level))
