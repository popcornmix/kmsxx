#!/usr/bin/python3 -i

# This is a base script for interactive libkms python environment

import pykms
from time import sleep
from math import sin
from math import cos

card = pykms.Card()

conn = card.get_first_connected_connector()

mode = conn.get_default_mode()

fb = pykms.DumbFramebuffer(card, 200, 200, "XR24");
pykms.draw_test_pattern(fb);

crtc = conn.get_current_crtc()

#crtc.set_mode(conn, fb, mode)

for p in crtc.get_possible_planes():
    if p.plane_type() == 0:
        plane = p
        break

def set_plane(x, y):
    crtc.set_plane(plane, fb, x, y, fb.width(), fb.height(), 0, 0, fb.width(), fb.height())

set_plane(0, 0)

# for x in range(0, crtc.width() - fb.width()): set_plane(x, int((sin(x/50) + 1) * 100)); sleep(0.01)
