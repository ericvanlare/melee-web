#!/usr/bin/env python3
"""Extract the pinned GALE01 original font atlas from a user's local DOL image."""
import argparse
import hashlib
from pathlib import Path
import struct
from extract_disc_file import DiscImage, DiscFormatError, ROOT

# config/GALE01/config.yml identifies this exact data range (287 * 512 bytes).
FONT_START=0x8040CD40
FONT_END=0x80430B40

def font_file_range(header: bytes) -> tuple[int,int]:
    if len(header)!=0x100:
        raise DiscFormatError("truncated DOL header")
    matches=[]
    for index in range(18):
        offset=struct.unpack_from(">I",header,index*4)[0]
        address=struct.unpack_from(">I",header,0x48+index*4)[0]
        size=struct.unpack_from(">I",header,0x90+index*4)[0]
        if size and address<=FONT_START and FONT_END<=address+size:
            if index<7 or offset<0x100 or address+size>0x100000000 or offset+size>0x100000000:
                raise DiscFormatError("font atlas is not inside a valid DOL data section")
            matches.append((offset+FONT_START-address,FONT_END-FONT_START))
    if len(matches)!=1:
        raise DiscFormatError("font atlas must resolve to exactly one complete DOL data section")
    return matches[0]

def extract(image: Path,output: Path) -> str:
    destination=output.resolve()
    if not destination.is_relative_to((ROOT/"assets-local").resolve()):
        raise DiscFormatError("output must be inside the ignored assets-local directory")
    with DiscImage(image) as disc:
        dol=struct.unpack(">I",disc.read(0x420,4))[0]
        if dol<0x440:
            raise DiscFormatError("invalid DOL file offset")
        offset,size=font_file_range(disc.read(dol,0x100))
        data=disc.read(dol+offset,size)
    destination.parent.mkdir(parents=True,exist_ok=True)
    with destination.open("xb") as file:
        file.write(data)
    return hashlib.sha256(data).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    try:
        digest=extract(args.image,args.output)
    except (OSError,DiscFormatError) as error:
        parser.exit(1,str(error)+"\n")
    print(f"Extracted {FONT_END-FONT_START} original font bytes; SHA256 {digest}")

if __name__=="__main__":
    main()
