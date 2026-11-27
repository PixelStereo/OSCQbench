# OSCQbench

**A Pixel Stereo application**, built on [pxs-qt](https://github.com/PixelStereo/pxs-qt).

<!-- release-downloads:start -->
No release yet.
<!-- release-downloads:end -->

On a **Mac**, unzip, move OSCQbench to Applications and Control-click › Open the first time (it is signed ad hoc, not notarized). On **Windows**, run the installer. On **Linux** and the **Raspberry Pi** (4 and 5, 64-bit), make the AppImage executable and run it (or unpack the tar.gz).

OSCQbench is both an OSCQuery device and a client for inspecting other devices.
Use the local fixture to exercise a controller such as Kadran or ossia score.
Connect to a remote endpoint to browse its namespace, probe HTTP attributes,
send OSC, and read the responses together with the exact packet bytes.

```sh
git clone --recurse-submodules https://github.com/PixelStereo/OSCQbench.git
cd OSCQbench
./build.sh
./build.sh -- --no-window --local
```

The build requires Qt 6.4 or newer with Qt WebSockets. The local fixture serves
OSCQuery on TCP 5679 and OSC on UDP 9100. Use `--http-port 0 --osc-port 0` for
ephemeral ports, or `--local` for a loopback-only device with mDNS disabled.
The executable's `--check` mode checks the fixture without opening a window.

## Workspace

- **Namespace.** Select a discovered device or type an HTTP endpoint. Filter
  the tree by address or type tag. Select a branch to work with its published
  nodes.
- **Controls.** Edit numeric and boolean parameters, send JSON arguments for
  other types, trigger actions, and see feedback. These controls send actual
  OSC messages using the transport selected in the composer. Remote feedback
  is available for nodes you subscribe to with **Listen**.
- **Node JSON and HTTP probe.** Inspect the selected node or query a single
  attribute such as `HOST_INFO`, `VALUE`, `RANGE`, `CLIPMODE`, `UNIT`,
  `EXTENDED_TYPE` or `OVERLOADS`. Enter custom attributes to inspect extensions.
- **Conformance.** Check address consistency, type tags, access and the shape
  of values and attributes, including nested arrays. This is a structural check.
- **Composer and traffic.** Enter tags and JSON arguments or paste hex for a
  raw packet or bundle. Send via UDP or WebSocket and inspect OSC, HTTP and
  WebSocket traffic. For `N` or `I`, enter `[null]`. For zero arguments,
  use empty tags and `[]`. Quote exact 64-bit integers. Enter timetags, MIDI
  or RGBA as hexadecimal strings where appropriate.

## Local test cases

| Path | Test |
|---|---|
| `/types` | Scalars, symbol, character, blob, time tag, MIDI bytes, packed RGBA, booleans and nested arrays |
| `/actions/none`, `/actions/infinitum`, `/signals/infinitum` | Writable `N` and `I`, and a read-only `I` |
| `/protocol/clip/{none,low,high,both}` | A 0–1 range with all four clipping modes. Send -1 and 2 and read the resulting value. |
| `/access`, `/values`, `/structure` | Access, locked values, defaults, tags, parameters with children, empty containers and mixed types |
| `/protocol/pulse`, `/protocol/emit` | Subscribe or unsubscribe to the pulse, then emit it on demand |
| `/protocol/toggle_path`, `/protocol/dynamic` | Add and remove a value to exercise namespace notifications |
| `/generator` | A controllable waveform and changing read-only signals |

For a controller check, add the local fixture by discovery or at
`http://127.0.0.1:5679`. Inspect the same nodes in both apps, send a write,
and compare the packet and resulting value in OSCQbench. To test in the other
direction, expose an OSCQuery device from the controller and enter its endpoint
in the bench. Optional extensions depend on the device under test.

Kadran's `oscqbench` CTest drives its Explorer against an independent Python
fixture using the golden vectors published by pxs-core.

References: [OSC 1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html),
[OSCQuery proposal](https://github.com/Vidvox/OSCQueryProposal), and
[ossia score OSCQuery device](https://ossia.io/score-docs/devices/oscquery-device.html).
