I think this module reads the data generated by openhd-system and setups:
1) Wifi cards to monitor mode if supported
2) Hotspot if a hotspot wifi card is there (for example the rpi included wifi card, which doesnt do monitor mode anyways)
3) Ethernet for a lan connection to the air/ground pi.

Since it needs the hw capabilities from openhd-system it MUST RUN AFTER openhd-system.

Hmm, I think this service also starts the wfb_tx / wfb_rx instances.

I think it is possible to have this service constrained by "run once at startup, then never again" paradigm.