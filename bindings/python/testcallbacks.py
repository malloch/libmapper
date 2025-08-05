#!/usr/bin/env python

import libmapper as mpr

print('starting testcallbacks.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

def h(obj, event, id):
    try:
        type = obj.type()
        print("got type: ", obj.type())
        if type is mpr.Type.DEVICE:
            print(event.name, 'device', obj['name'])
        elif type is mpr.Type.SIGNAL:
            val, time = obj.get_value()
            print(event.name, 'signal', obj['name'], val)
        elif type is mpr.Type.MAP:
            print(event.name, 'map:')
            for s in obj.signals(mpr.Map.Location.SOURCE):
                print("  src: ", s.device()['name'], ':', s['name'])
            for s in map.signals(mpr.Map.Location.DESTINATION):
                print("  dst: ", s.device()['name'], ':', s['name'])
        else:
            print("unknown type")
    except:
        print('exception')
        print(event.name)

src = mpr.Device("py.testcallbacks.src")
src.graph().add_callback(h)
outsig = src.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.FLOAT, None, 0, 1000)

dst = mpr.Device("py.testcallbacks.dst")
dst.graph().add_callback(h)
insig = dst.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.FLOAT, None, 0, 1)

while not src.ready or not dst.ready:
    src.poll()
    dst.poll(10)

map = mpr.Map(outsig, insig)
map['expr'] = "y=linear(x,0,100,0,3)"
map.push()

while not map.ready:
    src.poll(100)
    dst.poll(100)

for i in range(100):
    outsig.set_value(i)
    src.poll(10)
    dst.poll(10)

print('removing map')
map.release()

for i in range(100):
    outsig.set_value(i)
    src.poll(10)
    dst.poll(10)

print('freeing devices')
src.free()
dst.free()
print('done')
