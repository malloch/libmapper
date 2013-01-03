#!/usr/bin/env python

import sys, random, mapper

current_bar = -1

def h(metro, bar, beat):
    global current_bar
    try:
        print '--> metro played bar', bar, 'beat', beat
        if bar != current_bar:
            bpm = random.randrange(50, 200)
            count = random.randrange(1, 10)
            print 'setting: bpm=', bpm, ', count=', count
            metro.set_bpm(bpm, 1)
            metro.set_count(count, 1)
            current_bar = bar
    except:
        print 'exception'
        print metro, bar, beat

dev = mapper.device("dev")
m = dev.add_metronome("/foo", 0, 120, 4, h)

while not dev.ready():
    dev.poll()

for i in range(2000):
    dev.poll(10)
