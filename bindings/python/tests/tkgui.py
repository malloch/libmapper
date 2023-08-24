#!/usr/bin/env python

import tkinter

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

def on_gui_change(x):
    sig_out.set_value(int(x))

def on_change(sig, event, id, value, timetag):
    w.set(int(value))

dev = mpr.Device("tkgui")

sig_in = dev.add_signal(mpr.Direction.INCOMING, "input", 1, mpr.Type.INT32,
                        None, 0, 100, None, on_change)
sig_out = dev.add_signal(mpr.Direction.OUTGOING, "output", 1, mpr.Type.INT32,
                        None, 0, 100)

ui = tkinter.Tk()
ui.title("libmapper Python GUI demo")

name = tkinter.StringVar()
name.set("Waiting for device name...")
name_known = False
label = tkinter.Label(ui, textvariable=name)
label.pack()

w = tkinter.Scale(ui, from_=0, to=100, label='signal0',
                  orient=tkinter.HORIZONTAL, length=300,
                  command=on_gui_change)
w.pack()

def do_poll():
    global name_known
    dev.poll(20)
    if dev.ready and not name_known:
        name.set('Device name: %s, listening on port %s'%(dev[mpr.Property.NAME],
                 dev[mpr.Property.PORT]))
        name_known = True
    ui.after(5, do_poll)

do_poll()
ui.mainloop()
