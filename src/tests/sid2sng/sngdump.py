import sys

def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'GTS5'
    p = 4
    s = {}
    s['name'] = d[p:p+32]; s['author'] = d[p+32:p+64]; s['copy'] = d[p+64:p+96]; p += 96
    ns = d[p]; p += 1
    songs = []
    for i in range(ns):
        chans = []
        for c in range(3):
            n = d[p]; p += 1
            chans.append(list(d[p:p+n+1])); p += n + 1
        songs.append(chans)
    s['songs'] = songs
    ni = d[p]; p += 1
    ins = []
    for i in range(ni):
        ins.append(list(d[p:p+9]) + [d[p+9:p+25]]); p += 25
    s['instr'] = ins
    tables = []
    for t in range(4):
        n = d[p]; p += 1
        tables.append((list(d[p:p+n]), list(d[p+n:p+2*n]))); p += 2 * n
    s['tables'] = tables
    npat = d[p]; p += 1
    pats = []
    for i in range(npat):
        n = d[p]; p += 1
        pats.append([tuple(d[p+4*r:p+4*r+4]) for r in range(n)]); p += 4 * n
    s['patterns'] = pats
    assert p == len(d), (p, len(d))
    return s

if __name__ == '__main__':
    s = load(sys.argv[1])
    what = sys.argv[2] if len(sys.argv) > 2 else 'all'
    if what in ('all', 'tables'):
        for t, nm in enumerate('WPFS'):
            l, r = s['tables'][t]
            print(nm, ' '.join('%02x:%02x%02x' % (i + 1, l[i], r[i]) for i in range(len(l))))
    if what in ('all', 'instr'):
        for i, x in enumerate(s['instr']):
            print('I%02x' % (i + 1), ' '.join('%02x' % v for v in x[:9]), x[9].rstrip(b'\0'))
    if what in ('all', 'order'):
        for i, sg in enumerate(s['songs']):
            for c, o in enumerate(sg):
                print('S%d C%d' % (i, c), ' '.join('%02x' % v for v in o))
