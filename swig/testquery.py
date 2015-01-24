#!/usr/bin/env python

import sys, mapper

def h(sig, id, f, timetag):
    try:
        print '--> received query response:', f
    except:
        print 'exception'
        print sig, f

src = mapper.device("src")
outsig = src.add_output("/outsig", 1, 'f', None, 0, 1000)
outsig.set_callback(h)

dest = mapper.device("dest")
insig = dest.add_input("/insig", 1, 'f', None, 0, 1, h)

while not src.ready() or not dest.ready():
    src.poll()
    dest.poll(10)

monitor = mapper.monitor()

monitor.link('%s' %src.name, '%s' %dest.name)
while not src.num_links:
    src.poll(10)
    dest.poll(10)
monitor.connect('%s%s' %(src.name, outsig.name),
                '%s%s' %(dest.name, insig.name),
                {'mode': mapper.MO_LINEAR})
monitor.poll()

for i in range(100):
    print 'updating destination to', i, '-->'
    insig.update(i)
    outsig.query_remotes()
    src.poll(10)
    dest.poll(10)
