#!/usr/bin/env python

import libmapper as mpr

print('starting testmapfromstring.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

src = mpr.Device("py.testmapfromstr.src")
outsig = src.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32)

dest = mpr.Device("py.testmapfromstr.dst")
insig = dest.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT)
insig.add_callback(lambda s, e, i: print('signal', s['name'], 'got value', s.get_value()),
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
