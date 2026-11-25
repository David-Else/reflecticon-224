# reflecticon

This started as an emulation of one reverb machine from the service manual schematics: the 1978 Lexicon 224 (and 224x and 224xl).

I've since added four more early reverbs and delay units:
* AMS RMX16
* AMS DMX 15-80s
* Klark Teknik DN780
* Ursa Major StarGate 323

The Lexicon 224 has a web demo (you still need to supply the ROMs): https://joelanders.github.io/reflecticon/

* Makers and model names are named only to say what is emulated. This project is not affiliated with
or endorsed by any of them or their owners.

The Lexicon 224 is still the most interesting to me.

[Biography of the machine's designer](https://www.davidgriesinger.com/biographies/) (with photo of him prototyping on the bench!).

I'm still not sure what's more impressive: designing the reverb *algorithms* (initially without realtime rendering hardware) or
designing and building the *machine* that runs the algorithms. 1975-1978 predates DSP chips by a few years, so David Griesinger built a
programmable DSP out of a few boards of 7400 series TTL logic chips. An 8080 host computer writes ~100 32-bit instructions to a shared
Writable Control Store, and these run in a straight line once per audio frame. Throughput is one Delay Memory access and one MAC per instruction.

The web demo:

<img width="750" alt="The web page" src="https://github.com/user-attachments/assets/c0db1bfa-9a64-4a50-8ab3-c72e130a686f" />

The plugin:

<img width="560" alt="The plugin" src="https://github.com/user-attachments/assets/230c2c7c-5f06-43e8-8b63-90de70f8466e" />

## Layout

```
224/
  8080                 I made an 8080 emulator with https://github.com/floooh/chips
  sbc                  The board with the 8080 on it
  emulator             All the boards brought together
  juce-plugin          JUCE plugin
  analog               SPICE simulations of analog parts
  gate-level-verilog   1:1 with the chips on the schematics
  board-level-verilog  Higher abstraction level
  isa-level-verilog    Highest abstraction level
  isa-level-cpp        The fast version
  schematics
  web-demo
rmx16/  dmx1580/  dn780/  sg323/
  core/         the emulator
  plugin/       the plugin's machine definition and front panel
  tests/        check_machine: the machine against its ROMs
shared/         code the machines above share: the plugin framework (JUCE), ROM recognition,
                threading, the hardware-panel API, host checks
web/            one web page for rmx16, dmx1580, dn780 and sg323
notes/          notes that span machines
```


## Building

The 224: `make help` in `224/` lists the targets. Your ROMs go in `224/firmware/`.

The others, from this folder (JUCE 8 is fetched unless you point at a copy):

```
cmake -S . -B build -G Ninja [-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE]
cmake --build build
build/rmx16/rmx16_check_machine <folder with your RMX16 ROMs>
sh web/build.sh && python3 -m http.server -d web/page 8323
```

See [`shared/README.md`](shared/README.md) and [`web/README.md`](web/README.md).

## License

AGPL-3.0. Third-party code keeps its own licence (see the `LICENSE-*` files beside it).
