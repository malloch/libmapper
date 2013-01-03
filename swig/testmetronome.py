#!/usr/bin/env python

import sys, mapper

def h(metro, bar, beat):
    try:
        print '--> metro played bar', bar, 'beat', beat
    except:
        print 'exception'
        print metro, bar, beat

dev = mapper.device("dev")
m = dev.add_metronome("/foo", 0, 120, 4, h)

while not dev.ready():
    dev.poll()

for i in range(1000):
    dev.poll(10)
