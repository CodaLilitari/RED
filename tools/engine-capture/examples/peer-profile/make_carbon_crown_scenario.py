"""Make the two-start test variant from an author's existing exact Carbon Crown Exchange package."""
import argparse
import hashlib
from pathlib import Path
import re
import struct


def make(source):
    if not 0<source.stat().st_size<=256<<20:
        raise ValueError("Unsupported source size")
    blob=source.read_bytes()
    if hashlib.sha256(blob).hexdigest()!="b960e0b3ac403e1bc0de5cef8e91d5ddcfb80e8c8e7137bd3227f5664cbf16fe":
        raise ValueError("The input differs from the original authored build for this recipe")
    directory=struct.unpack_from("<I",blob,8)[0]
    count=struct.unpack_from("<I",blob,directory)[0]
    entries={}
    for offset,size,name in struct.iter_unpack("<II128s",blob[directory+4:directory+4+136*count]):
        entries[name.partition(b"\0")[0].decode()]=blob[offset:offset+size]
    key=next(k for k in entries if k.lower().endswith(".jkl"))
    level=entries[key].decode("latin1")
    templates=re.search(r"(?ims)^World templates (\d+)([^\n]*\n)(.*?)^end[ \t\r]*$",level)
    level=(level[:templates.start()]+f"World templates {int(templates[1])+1}"+templates[2]+templates[3]
           +"redmarker none type=ghost\nend"+level[templates.end():])
    layout={"spawn_N0":(0,5.94,0),"spawn_N1":(0,6.66,180),"shield_N":(0,6.48,0),"shield_E":(0,6.12,0),
            "det_N":(0,6.47,0),"det_E":(0,6.13,0),"det_S":(.19,6.66,0),"det_W":(-.19,5.94,0)}
    def place(match):
        idx,template,name=match.group(1,2,3)
        if name in layout:
            x,y,yaw=layout[name]
            respawn="" if template=="walkplayer" else " respawn=3"
            return f"{idx}: {template} {name} {x} {y} .12 0 {yaw} 0 34{respawn}"
        if template=="walkplayer":
            return match[0].replace(template,"redmarker",1)
        return match[0]
    first,things=level.split("World things ",1)
    entries[key]=(first+"World things "+re.sub(r"(?m)^(\d+):\s+(\S+)\s+(\S+)[^\n]*$",place,things)).encode("latin1")
    offset=16+len(entries)*136;directory=bytearray();payload=bytearray()
    for name,data in entries.items():
        directory.extend(struct.pack("<II128s",offset+len(payload),len(data),name.encode()))
        payload.extend(data)
    result=b"GOB "+struct.pack("<III",20,12,len(entries))+directory+payload
    if hashlib.sha256(result).hexdigest()!="512fb95bc80e88b919713c0a9e447d89a8235f2a174af3d672ab6a9d6c48dbd8":
        raise ValueError("The derived test package differs from the measured package")
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    result=make(args.original)
    with args.output.open("xb") as target:
        target.write(result)
