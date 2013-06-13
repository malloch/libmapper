
import sys, mapper

def h(sig, id, f, timetag):
    try:
        print sig.name, f
    except:
        print 'exception'
        print sig, f

dev = mapper.device("test")
sigin = dev.add_input("/freq", 1, 'i', "Hz", None, None, h)
sigout = dev.add_output("/freq2", 1, 'i', "Hz", -10, 10)
print 'num_inputs', dev.num_inputs
print 'num_outputs', dev.num_outputs


while not dev.ready:
    dev.poll(10)

for i in range(1000):
    dev.poll(10)
    sigout.update(i)
