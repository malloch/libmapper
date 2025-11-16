#!/usr/bin/env python

import random, libmapper as mpr

print('starting testinstance.py')
print('libmapper version:', mpr.__version__, 'with' if mpr.has_numpy() else 'without', 'numpy support')

def h(sig, event, id, val, time):
    try:
        if event == mpr.Signal.Event.REMOTE_UPDATE:
            print('--> destination instance', id, 'got', val)
        elif event == mpr.Signal.Event.UPSTREAM_RELEASE:
            print('--> retiring destination instance', id)
            sig.Instance(id).release()
    except:
        print('--> exception!')
        print(sig, event, id, val)

def print_instance_ids():
    phrase = 'active /outsig: ['
    for i in outsig.instances(mpr.Signal.Status.HAS_VALUE):
        phrase += ' '
        phrase += str(i.id)
    phrase += ' ]   '
    phrase += 'active /insig: ['
    for i in insig.instances(mpr.Signal.Status.HAS_VALUE):
        phrase += ' '
        phrase += str(i.id)
    phrase += ' ]'
    print(phrase)

def print_instance_values():
    phrase = 'active /outsig: ['
    for i in outsig.instances(mpr.Object.Status.HAS_VALUE):
        phrase += ' '
        phrase += str(i.get_value()[0])
    phrase += ' ]   '
    phrase += 'active /insig: ['
    for i in insig.instances(mpr.Object.Status.HAS_VALUE):
        phrase += '   '
        phrase += str(i.get_value()[0])
    phrase += ' ]'
    print(phrase)

def print_instance_timetags():
    phrase = 'active /outsig: ['
    for i in outsig.instances(mpr.Object.Status.HAS_VALUE):
        phrase += ' '
        phrase += str(i.get_value()[1].get_double())
    phrase += ' ]   '
    phrase += 'active /insig: ['
    for i in insig.instances(mpr.Object.Status.HAS_VALUE):
        phrase += '   '
        phrase += str(i.get_value()[1].get_double())
    phrase += ' ]'
    print(phrase)

def print_instance_statuses():
    phrase = 'active /outsig: ['
    for i in outsig.instances(mpr.Object.Status.HAS_VALUE):
        phrase += ' '
        phrase += str(i.get_status())
    phrase += ' ]   '
    phrase += 'active /insig: ['
    for i in insig.instances(mpr.Object.Status.HAS_VALUE):
        phrase += '   '
        phrase += str(i.get_status())
    phrase += ' ]'
    print(phrase)

src = mpr.Device("py.testinstance.src")
outsig = src.add_signal(mpr.Signal.Direction.OUTGOING, "outsig", 1, mpr.Type.INT32, None, 0, 100, 5)
outsig.reserve_instances(5)
outsig.set_property(mpr.Property.EPHEMERAL, True)

dest = mpr.Device("py.testinstance.dst")
# reserve 0 instances to start so we can use custom indexes
insig = dest.add_signal(mpr.Signal.Direction.INCOMING, "insig", 1, mpr.Type.INT32, None, 0, 1, 0, h,
                        mpr.Signal.Event.ANY)
insig.reserve_instances([100, 200, 300])
insig.set_property(mpr.Property.STEALING, mpr.Signal.Stealing.OLDEST)
insig.set_property(mpr.Property.EPHEMERAL, True)

while not src.ready or not dest.ready:
    src.poll()
    dest.poll(10)

map = mpr.Map(outsig, insig).set_property(mpr.Property.EXPRESSION, "y=x").push()

while not map.ready:
    src.poll(10)
    dest.poll(10)

for i in range(100):
    r = random.randint(0,5)
    id = random.randint(0,5)
    if r == 0:
        print('--> retiring sender instance', id)
        outsig.Instance(id).release()
    else:    
        print('--> sender instance', id, 'updated to', r)
        outsig.Instance(id).set_value(r)
    print_instance_ids()
    print_instance_values()
    print_instance_timetags()
    print_instance_statuses()
    dest.poll(100)
    src.poll(0)

print('insig status:', insig.get_status())
print('freeing devices')
src.free()
dest.free()
print('done')
