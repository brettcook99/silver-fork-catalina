# Structural comparison of an original .sng with the one sid2sng -exact got
# back from the packed SID. Pattern, instrument & table numbering differ
# (the packer renumbers and drops unused things), so the comparison maps
# the numbers by where they are used and checks the mapping is consistent.
import sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from sngdump import load

REST, KEYOFF, ENDPATT = 0xbd, 0xbe, 0xff
errors = []

def err(msg):
    if len(errors) < 20:
        errors.append(msg)

class Mapping:
    def __init__(self, name):
        self.name = name; self.fwd = {}
    def check(self, a, b, where):
        if a in self.fwd and self.fwd[a] != b:
            err('%s %02x maps to both %02x and %02x (%s)' % (self.name, a, self.fwd[a], b, where))
        self.fwd[a] = b

def orderentries(o):
    # Orderlist without the endmark/restart; R0 (repeat 16, dropped by the
    # packer) left out
    body = o[:o.index(0xff)] if 0xff in o else o
    return [x for x in body if x != 0xd0], o[-1]

def tableprogram(s, t, ptr, limit=300):
    # Rows reached from ptr until stop or a repeated row (loop), with jumps as
    # offsets into the program so programs compare position-independently
    l, r = s['tables'][t]
    rows = []; seen = {}
    while ptr and ptr <= len(l) and ptr not in seen and len(rows) < limit:
        seen[ptr] = len(rows)
        if t == 3:  # speedtable: one row
            rows.append((l[ptr-1], r[ptr-1])); break
        if l[ptr-1] == 0xff:
            tgt = r[ptr-1]
            rows.append(('jump', seen.get(tgt, 'out') if tgt else 'stop'))
            if not tgt or tgt in seen: break
            ptr = tgt; continue
        rows.append((l[ptr-1], r[ptr-1]))
        ptr += 1
    return rows

def normcmd(cmd, data, instrmap_unused=None):
    return cmd, data

def compare(orig, back):
    so, sb = orig['songs'], back['songs']
    if len(so) != len(sb): err('subtunes %d vs %d' % (len(so), len(sb)))
    pmap = Mapping('pattern'); imap = Mapping('instrument')
    smap = Mapping('speedrow'); wmap = Mapping('waverow'); ppmap = Mapping('pulserow'); fmap = Mapping('filterrow')
    tmaps = [wmap, ppmap, fmap, smap]
    for si, (co, cb) in enumerate(zip(so, sb)):
        for c in range(3):
            eo, ro = orderentries(co[c]); eb, rb = orderentries(cb[c])
            if len(eo) != len(eb): err('S%d C%d orderlist length %d vs %d' % (si, c, len(eo), len(eb))); continue
            if ro != rb: err('S%d C%d restart %02x vs %02x' % (si, c, ro, rb))
            for x, y in zip(eo, eb):
                if x < 0xd0: pmap.check(x, y, 'S%d C%d' % (si, c))
                elif x != y: err('S%d C%d orderlist command %02x vs %02x' % (si, c, x, y))
    # Patterns, row by row; instruments with their "current instrument" carried
    # through the pattern (the packer leaves out repeated instrument numbers)
    for po, pb in pmap.fwd.items():
        ro, rb = orig['patterns'][po], back['patterns'][pb]
        ro = ro[:[x[0] for x in ro].index(ENDPATT)] if ENDPATT in [x[0] for x in ro] else ro
        rb = rb[:[x[0] for x in rb].index(ENDPATT)] if ENDPATT in [x[0] for x in rb] else rb
        if len(ro) != len(rb): err('pattern %02x/%02x rows %d vs %d' % (po, pb, len(ro), len(rb))); continue
        curo = curb = 0
        for i, (a, b) in enumerate(zip(ro, rb)):
            where = 'pattern %02x row %d' % (po, i)
            if a[0] != b[0]: err('%s note %02x vs %02x' % (where, a[0], b[0]))
            if a[1] and a[1] != curo: curo = a[1];
            if b[1]: curb = b[1]
            if a[1] or b[1]:
                if curo == 0 or curb == 0: err('%s instrument %02x vs %02x' % (where, a[1], b[1]))
                else: imap.check(curo, curb, where)
            ca, da = a[2], a[3]; cb_, db = b[2], b[3]
            if ca == 0: da = 0
            if ca == 0xd and da > 0x0f: ca = da = 0  # timing marks dropped without author info
            if ca == 0xf and (da & 0x7f) == 2: da += 1
            if ca != cb_: err('%s command %x vs %x' % (where, ca, cb_)); continue
            if ca in (1, 2, 3, 4, 0xe): tmaps[3].check(da, db, where) if da else (db == 0 or err(where + ' speed 0'))
            elif ca in (8, 9, 0xa): tmaps[ca - 8].check(da, db, where) if da else (db == 0 or err(where + ' ptr 0'))
            elif da != db: err('%s command %x data %02x vs %02x' % (where, ca, da, db))
    # Instruments
    for io, ib in imap.fwd.items():
        a, b = orig['instr'][io-1], back['instr'][ib-1]
        where = 'instrument %02x/%02x' % (io, ib)
        for k, nm in ((0, 'AD'), (1, 'SR'), (8, 'firstwave')):
            if a[k] != b[k]: err('%s %s %02x vs %02x' % (where, nm, a[k], b[k]))
        if (a[7] & 0xc0) != (b[7] & 0xc0) or (a[7] & 0x3f) != (b[7] & 0x3f): err('%s gatetimer %02x vs %02x' % (where, a[7], b[7]))
        for t in range(3):
            if bool(a[2+t]) != bool(b[2+t]): err('%s table %d pointer %02x vs %02x' % (where, t, a[2+t], b[2+t]))
            elif a[2+t]: tmaps[t].check(a[2+t], b[2+t], where)
        if a[6]:
            if a[6] != b[6]: err('%s vibdelay %02x vs %02x' % (where, a[6], b[6]))
            if a[5]: tmaps[3].check(a[5], b[5], where)
    # Table programs reached from the mapped pointers
    for t in range(4):
        for po, pb in tmaps[t].fwd.items():
            A = tableprogram(orig, t, po); B = tableprogram(back, t, pb)
            if t == 2:
                # The packer keeps only the passband bits of "set filter" rows
                A = [((x[0] & 0xf0), x[1]) if isinstance(x[0], int) and 0x80 <= x[0] < 0xff else x for x in A]
            if A != B: err('table %d program %02x/%02x differs: %s vs %s' % (t, po, pb, A[:6], B[:6]))
    return pmap, imap

if __name__ == '__main__':
    o = load(sys.argv[1]); b = load(sys.argv[2])
    pmap, imap = compare(o, b)
    if errors:
        print('DIFFERENT: ' + '; '.join(errors))
        sys.exit(1)
    print('same (%d patterns, %d instruments compared)' % (len(pmap.fwd), len(imap.fwd)))
