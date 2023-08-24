#!/usr/bin/env python

try:
    import libmapper as mpr
except:
    import sys, os
    try:
        # Try the "../src" directory, relative to the location of this
        # program, which is where it should be if the module has not been installed.
        sys.path.append(
                        os.path.join(os.path.join(os.getcwd(),
                                                  os.path.dirname(sys.argv[0])),
                                     '../src'))
        import libmapper as mpr
    except:
        print('Error importing libmapper module.')
        sys.exit(1)

print('starting testrefcount.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

def h(sig, event, id, val, time):
    print('  handler got', sig['name'], '=', val, 'at time', time.get_double())

src = mpr.Device('py.testrefcount.src')
outsig = src.add_signal(mpr.Direction.OUTGOING, 'outsig', 10, mpr.Type.INT32, None, 0, 1)

dest = mpr.Device('py.testrefcount.dst')

num_dst = 5

for i in range(num_dst):
    # don't store references to input signals
    dest.add_signal(mpr.Direction.INCOMING, 'insig%i' %i, 10, mpr.Type.FLOAT, None, 0, 1, None, h)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

for s in dest.signals():
    map = mpr.Map(outsig, s).push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.set_value([i, i+1, i+2, i+3, i+4, i+5, i+6, i+7, i+8, i+9])
    dest.poll(10)
    src.poll(10)

print('freeing devices')
src.free()
dest.free()
print('done')
