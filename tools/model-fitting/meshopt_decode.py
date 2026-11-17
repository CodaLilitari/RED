"""Decode a GLB that uses EXT_meshopt_compression into a plain GLB that Blender's glTF importer can read.

usage: python meshopt_decode.py IN.glb OUT.glb

Tripo's model links are meshopt-compressed GLBs, and Blender's importer refuses the extension. This decodes every
compressed buffer view (attribute, triangle and index modes, with the octahedral, quaternion and exponential filters)
following the bitstream in the EXT_meshopt_compression specification, and writes the decoded bytes into one plain
binary buffer. KHR_mesh_quantization is left in place; Blender reads it.
"""
import json
import math
import struct
import sys

import numpy as np


# ---------------------------------------------------------------------------------------------------- vertex codec

def _decode_group(data, pos, mode):
    """One group of 16 bytes for one byte lane. Returns (values, new position)."""
    if mode == 0:
        return [0] * 16, pos
    if mode == 3:
        return list(data[pos:pos + 16]), pos + 16
    bits = 2 if mode == 1 else 4
    packed = 4 if mode == 1 else 8
    sentinel = (1 << bits) - 1
    out = []
    extra = pos + packed
    for i in range(packed):
        byte = data[pos + i]
        for shift in range(8 - bits, -1, -bits):
            v = (byte >> shift) & sentinel
            if v == sentinel:
                v = data[extra]
                extra += 1
            out.append(v)
    return out, extra


def decode_vertex_buffer(data, count, stride):
    if data[0] & 0xF0 != 0xA0:
        raise ValueError('not a meshopt vertex stream')
    version = data[0] & 0x0F
    if version not in (0,):
        raise ValueError(f'vertex codec version {version} is not supported')
    tail = max(stride, 32)
    last = bytearray(data[len(data) - stride:])
    block = min((8192 // stride) & ~15, 256)
    out = bytearray(count * stride)
    pos = 1
    end = len(data) - tail
    done = 0
    while done < count:
        n = min(block, count - done)
        groups = (n + 15) // 16
        for k in range(stride):
            header_size = (groups + 3) // 4
            header = data[pos:pos + header_size]
            pos += header_size
            lane = []
            for g in range(groups):
                mode = (header[g // 4] >> ((g % 4) * 2)) & 3
                vals, pos = _decode_group(data, pos, mode)
                lane.extend(vals)
            p = last[k]
            for i in range(n):
                v = lane[i]
                delta = (v >> 1) ^ (-(v & 1) & 0xFF)
                p = (p + delta) & 0xFF
                out[(done + i) * stride + k] = p
            last[k] = p
        done += n
        if pos > end:
            raise ValueError('vertex stream overruns its tail')
    return bytes(out)


# ----------------------------------------------------------------------------------------------------- index codec

def _vbyte(data, pos):
    result = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if b < 0x80:
            return result, pos
        shift += 7


def _decode_index(data, pos, last):
    v, pos = _vbyte(data, pos)
    d = (v >> 1) ^ -(v & 1)
    return (last + d) & 0xFFFFFFFF, pos


def decode_index_buffer(data, count, index_size):
    if data[0] & 0xF0 != 0xE0:
        raise ValueError('not a meshopt index stream')
    version = data[0] & 0x0F
    if version > 1:
        raise ValueError(f'index codec version {version} is not supported')
    fecmax = 13 if version >= 1 else 15
    tri_count = count // 3
    codes = data[1:1 + tri_count]
    pos = 1 + tri_count
    aux_table = data[len(data) - 16:]
    edge = [[0xFFFFFFFF, 0xFFFFFFFF] for _ in range(16)]
    eoff = 0
    vfifo = [0xFFFFFFFF] * 16
    voff = 0
    nxt = 0
    last = 0
    out = []

    def push_v(v, cond=True):
        nonlocal voff
        vfifo[voff] = v
        voff = (voff + (1 if cond else 0)) & 15

    def push_e(a, b):
        nonlocal eoff
        edge[eoff][0] = a
        edge[eoff][1] = b
        eoff = (eoff + 1) & 15

    for code in codes:
        if code < 0xF0:
            fe = code >> 4
            a, b = edge[(eoff - 1 - fe) & 15]
            fec = code & 15
            if fec < fecmax:
                c = nxt if fec == 0 else vfifo[(voff - 1 - fec) & 15]
                if fec == 0:
                    nxt += 1
                out += (a, b, c)
                push_v(c, fec == 0)
                push_e(c, b)
                push_e(a, c)
            else:
                if fec != 15:
                    c = (last + (fec - (fec ^ 3))) & 0xFFFFFFFF
                else:
                    c, pos = _decode_index(data, pos, last)
                last = c
                out += (a, b, c)
                push_v(c)
                push_e(c, b)
                push_e(a, c)
        elif code < 0xFE:
            aux = aux_table[code & 15]
            feb, fec = aux >> 4, aux & 15
            a = nxt
            nxt += 1
            b = nxt if feb == 0 else vfifo[(voff - feb) & 15]
            if feb == 0:
                nxt += 1
            c = nxt if fec == 0 else vfifo[(voff - fec) & 15]
            if fec == 0:
                nxt += 1
            out += (a, b, c)
            push_v(a)
            push_v(b, feb == 0)
            push_v(c, fec == 0)
            push_e(b, a)
            push_e(c, b)
            push_e(a, c)
        else:
            aux = data[pos]
            pos += 1
            fea = 0 if code == 0xFE else 15
            feb, fec = aux >> 4, aux & 15
            if aux == 0:
                nxt = 0
            if fea == 0:
                a = nxt
                nxt += 1
            else:
                a = 0
            if feb == 0:
                b = nxt
                nxt += 1
            else:
                b = vfifo[(voff - feb) & 15]
            if fec == 0:
                c = nxt
                nxt += 1
            else:
                c = vfifo[(voff - fec) & 15]
            if fea == 15:
                a, pos = _decode_index(data, pos, last)
                last = a
            if feb == 15:
                b, pos = _decode_index(data, pos, last)
                last = b
            if fec == 15:
                c, pos = _decode_index(data, pos, last)
                last = c
            out += (a, b, c)
            push_v(a)
            push_v(b, feb in (0, 15))
            push_v(c, fec in (0, 15))
            push_e(b, a)
            push_e(c, b)
            push_e(a, c)
    fmt = '<%d%s' % (len(out), 'H' if index_size == 2 else 'I')
    return struct.pack(fmt, *out)


def decode_index_sequence(data, count, index_size):
    if data[0] & 0xF0 != 0xD0:
        raise ValueError('not a meshopt index sequence')
    pos = 1
    last = [0, 0]
    out = []
    for _ in range(count):
        v, pos = _vbyte(data, pos)
        current = v & 1
        v >>= 1
        d = (v >> 1) ^ -(v & 1)
        last[current] = (last[current] + d) & 0xFFFFFFFF
        out.append(last[current])
    fmt = '<%d%s' % (len(out), 'H' if index_size == 2 else 'I')
    return struct.pack(fmt, *out)


# --------------------------------------------------------------------------------------------------------- filters

def filter_octahedral(raw, count, stride):
    if stride == 4:
        a = np.frombuffer(raw, dtype=np.int8).reshape(count, 4).astype(np.float64)
        scale = 127.0
        dtype = np.int8
    else:
        a = np.frombuffer(raw, dtype=np.int16).reshape(count, 4).astype(np.float64)
        scale = 32767.0
        dtype = np.int16
    one = a[:, 2].copy()
    one[one == 0] = 1.0
    x = a[:, 0] / one
    y = a[:, 1] / one
    z = 1.0 - np.abs(x) - np.abs(y)
    t = np.maximum(-z, 0.0)
    x = x - np.where(x >= 0, t, -t)
    y = y - np.where(y >= 0, t, -t)
    length = np.sqrt(x * x + y * y + z * z)
    length[length == 0] = 1.0
    out = a.copy()
    for i, v in enumerate((x, y, z)):
        out[:, i] = np.round(v / length * scale)
    return out.astype(dtype).tobytes()


def filter_quaternion(raw, count, stride):
    if stride != 8:
        raise ValueError('quaternion filter needs 16-bit components')
    a = np.frombuffer(raw, dtype=np.int16).reshape(count, 4).astype(np.int64)
    scale = 1.0 / math.sqrt(2.0)
    sf = a[:, 3]
    ss = (sf | 3).astype(np.float64)
    qx = a[:, 0] * scale / ss
    qy = a[:, 1] * scale / ss
    qz = a[:, 2] * scale / ss
    qw = np.sqrt(np.maximum(0.0, 1.0 - qx * qx - qy * qy - qz * qz))
    qc = (sf & 3)
    out = np.zeros((count, 4), dtype=np.int16)
    comps = [qx, qy, qz]
    for row in range(count):
        q = [0.0, 0.0, 0.0, 0.0]
        c = int(qc[row])
        q[(c + 1) & 3] = comps[0][row]
        q[(c + 2) & 3] = comps[1][row]
        q[(c + 3) & 3] = comps[2][row]
        q[c] = qw[row]
        out[row] = [int(round(v * 32767.0)) for v in q]
    return out.tobytes()


def filter_exponential(raw, count, stride):
    a = np.frombuffer(raw, dtype=np.int32).astype(np.int64)
    e = a >> 24
    m = ((a & 0xFFFFFF) ^ 0x800000) - 0x800000
    values = (m.astype(np.float64) * np.power(2.0, e.astype(np.float64))).astype(np.float32)
    return values.tobytes()


FILTERS = {'NONE': None, 'OCTAHEDRAL': filter_octahedral, 'QUATERNION': filter_quaternion,
           'EXPONENTIAL': filter_exponential}


# ------------------------------------------------------------------------------------------------------------ GLB

def read_glb(path):
    b = open(path, 'rb').read()
    magic, _version, _length = struct.unpack('<III', b[:12])
    if magic != 0x46546C67:
        raise ValueError('not a GLB')
    pos = 12
    gltf = None
    binary = b''
    while pos < len(b):
        clen, ctype = struct.unpack('<II', b[pos:pos + 8])
        chunk = b[pos + 8:pos + 8 + clen]
        if ctype == 0x4E4F534A:
            gltf = json.loads(chunk)
        elif ctype == 0x004E4942:
            binary = chunk
        pos += 8 + clen
    return gltf, binary


def write_glb(path, gltf, binary):
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * ((4 - len(js) % 4) % 4)
    binary += b'\0' * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack('<II', len(binary), 0x004E4942))
        f.write(binary)


def decode_glb(src, dst):
    gltf, binary = read_glb(src)
    buffers = gltf['buffers']
    if any('uri' in buf for buf in buffers):
        raise ValueError('only self-contained GLBs are handled')
    if len([buf for buf in buffers if not buf.get('extensions', {}).get('EXT_meshopt_compression', {}).get('fallback')]) > 1:
        raise ValueError('more than one data buffer')

    def source(buffer_index, offset, length):
        if buffer_index != 0 and buffers[buffer_index].get('extensions', {}).get('EXT_meshopt_compression', {}).get('fallback'):
            raise ValueError('compressed data in a fallback buffer')
        return binary[offset:offset + length]

    out = bytearray()
    views = []
    decoded = 0
    for view in gltf['bufferViews']:
        ext = view.get('extensions', {}).get('EXT_meshopt_compression')
        if ext:
            data = source(ext['buffer'], ext.get('byteOffset', 0), ext['byteLength'])
            count, stride, mode = ext['count'], ext['byteStride'], ext['mode']
            if mode == 'ATTRIBUTES':
                raw = decode_vertex_buffer(data, count, stride)
            elif mode == 'TRIANGLES':
                raw = decode_index_buffer(data, count, stride)
            elif mode == 'INDICES':
                raw = decode_index_sequence(data, count, stride)
            else:
                raise ValueError(f'unknown mode {mode}')
            flt = FILTERS[ext.get('filter', 'NONE')]
            if flt:
                raw = flt(raw, count, stride)
            if len(raw) != count * stride:
                raise ValueError('decoded size mismatch')
            decoded += 1
            new = {k: v for k, v in view.items() if k not in ('extensions', 'buffer', 'byteOffset', 'byteLength')}
            if mode == 'ATTRIBUTES' and 'byteStride' not in new and stride % 4 == 0:
                new['byteStride'] = stride
        else:
            raw = source(view['buffer'], view.get('byteOffset', 0), view['byteLength'])
            new = {k: v for k, v in view.items() if k not in ('buffer', 'byteOffset', 'byteLength')}
        while len(out) % 4:
            out.append(0)
        new['buffer'] = 0
        new['byteOffset'] = len(out)
        new['byteLength'] = len(raw)
        out += raw
        views.append(new)
    gltf['bufferViews'] = views
    gltf['buffers'] = [{'byteLength': len(out)}]
    for key in ('extensionsUsed', 'extensionsRequired'):
        if key in gltf:
            gltf[key] = [e for e in gltf[key] if e != 'EXT_meshopt_compression']
            if not gltf[key]:
                del gltf[key]
    write_glb(dst, gltf, bytes(out))
    return decoded


def check(path):
    """Sanity checks on the decoded file: index ranges and unit normals."""
    gltf, binary = read_glb(path)
    comp = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
    ncomp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

    def accessor(i):
        acc = gltf['accessors'][i]
        view = gltf['bufferViews'][acc['bufferView']]
        fmt, size = comp[acc['componentType']]
        n = ncomp[acc['type']]
        stride = view.get('byteStride', size * n)
        base = view['byteOffset'] + acc.get('byteOffset', 0)
        rows = []
        for r in range(acc['count']):
            rows.append(struct.unpack_from('<%d%s' % (n, fmt), binary, base + r * stride))
        return acc, rows

    report = []
    for mesh in gltf['meshes']:
        for prim in mesh['primitives']:
            pacc, pos = accessor(prim['attributes']['POSITION'])
            count = pacc['count']
            if 'indices' in prim:
                _, idx = accessor(prim['indices'])
                hi = max(i[0] for i in idx)
                report.append(f'{len(idx) // 3} triangles over {count} vertices, max index {hi}')
                if hi >= count:
                    raise ValueError('index out of range')
            if 'NORMAL' in prim['attributes']:
                nacc, nrm = accessor(prim['attributes']['NORMAL'])
                scale = {5120: 127.0, 5122: 32767.0, 5126: 1.0}[nacc['componentType']]
                lengths = [math.sqrt(sum((c / scale) ** 2 for c in v[:3])) for v in nrm[::max(1, len(nrm) // 2000)]]
                report.append(f'normal length {min(lengths):.3f}..{max(lengths):.3f}')
    return report


if __name__ == '__main__':
    n = decode_glb(sys.argv[1], sys.argv[2])
    print(f'decoded {n} buffer views')
    for line in check(sys.argv[2]):
        print(line)
