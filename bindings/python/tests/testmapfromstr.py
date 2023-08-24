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

print('starting testmapfromstring.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

src = mpr.Device("py.testmapfromstr.src")
outsig = src.add_signal(mpr.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32)

dest = mpr.Device("py.testmapfromstr.dst")
insig = dest.add_signal(mpr.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT,
                        None, None, None, None,
                        lambda s, e, i, v, t: print('signal', s['name'], 'got value',
                                                    v, 'at time', t.get_double()),
                        mpr.Signal.Event.UPDATE)

while not src.ready or not dest.ready:
    src.poll(10)
    dest.poll(10)

map = mpr.Map("%y=(%x+100)*2", insig, outsig)
map.push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    outsig.set_value(i)
    dest.poll(10)
    src.poll(0)

print('freeing devices')
src.free()
dest.free()
print('done')
