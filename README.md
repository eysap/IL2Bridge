# Il2Bridge

Il2Bridge is an experimental Linux instrumentation toolkit for Unity IL2CPP
applications. A small C loader runs inside the target process, while a separate
C++20 broker discovers live sessions, resolves managed methods, manages native
hooks, and consumes events over a restricted Unix socket.

The project currently supports passive runtime discovery, metadata-token-based
method identity, reversible x86-64 hooks, call counters, and bounded event
streaming. Typed method invocation and transactional hook manifests are not
implemented yet.

> [!WARNING]
> Il2Bridge patches executable code in a live process and relies on both public
> IL2CPP exports and one documented internal layout assumption. It is a research
> and engineering project, not a production-ready instrumentation framework.

Use it only on software and systems you own or are explicitly authorized to
instrument.

## What works today

- Passive discovery of an already-mapped `GameAssembly.so`; the loader does not
  load the module itself.
- Runtime resolution of images, classes, and methods through resolved
  `il2cpp_*` APIs.
- Stable method selectors based on metadata tokens, with a readable name/arity
  fallback that rejects ambiguous matches.
- Transparent `around` counter probes and explicit `replace` hooks.
- Opaque IPC handles: clients cannot submit arbitrary process addresses.
- Target discovery and session identity based on PID plus process start time.
- Human-readable and JSON broker output.
- A fixed-capacity event ring with resumable sequence numbers and loss
  reporting.
- Unit tests plus an end-to-end `LD_PRELOAD` integration fixture.

## Architecture

```text
┌──────────────────────── target process ────────────────────────┐
│                                                                │
│  IL2CPP runtime ← bridge ← hook registry ← bounded event ring  │
│                         il2bridge-loader (C)                    │
└──────────────────────────────┬─────────────────────────────────┘
                               │ private Unix socket
                               │ opaque handles + peer validation
┌──────────────────────────────▼─────────────────────────────────┐
│                    il2bridge broker (C++20)                    │
│  discovery · canonical selectors · hook lifecycle · events    │
└────────────────────────────────────────────────────────────────┘
```

The loader owns every operation that touches target memory. The broker owns
discovery, validation, user-facing identities, and output formatting. See
[Architecture](docs/architecture.md) for the lifecycle, threading model, and
hooking invariants.

## Build and test

Requirements:

- Linux on x86-64;
- CMake 3.20 or newer;
- a C11 and C++20 toolchain;
- Git and network access for CMake's third-party dependencies.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The resulting binaries are:

```text
build/loader/libil2bridge-loader.so
build/broker/il2bridge
```

## Quick start

### 1. Load the instrumentation library

Start an IL2CPP application with the loader preloaded:

```bash
LD_PRELOAD="$PWD/build/loader/libil2bridge-loader.so" \
  /path/to/il2cpp-application
```

For applications started by another launcher, propagate the same `LD_PRELOAD`
value through that launcher's environment. The loader waits for
`GameAssembly.so` to appear in `/proc/self/maps`, obtains an `RTLD_NOLOAD`
handle, resolves its required API, and only then exposes IPC.

### 2. Discover the target

```console
$ IL2BRIDGE=./build/broker/il2bridge
$ "$IL2BRIDGE" targets list
42420:31147059  pid=42420  socket=/run/user/1000/il2bridge/42420.sock

$ "$IL2BRIDGE" status
Il2Bridge target 42420:31147059
PID: 42420
Protocol: 1
Socket: /run/user/1000/il2bridge/42420.sock
Active hooks: 0
Events: next=1 capacity=1024 dropped=0
```

Automatic selection works when exactly one target is available. Use
`--pid PID` or `--socket PATH` before the command when several sessions are
running:

```bash
$IL2BRIDGE --pid 12345 status
```

### 3. Install and observe a counter probe

Methods use the canonical form
`Assembly!Namespace.Type::Method@0xTOKEN`. Installing a transparent counter
probe returns its session-bound hook ID:

```console
$ "$IL2BRIDGE" hook add \
    'Sample.dll!Example.Input.Controller::ValidateInput@0x06001234' \
    --mode around --handler count-calls
Hook 42420:31147059:0 installed
Method: Sample.dll!Example.Input.Controller::ValidateInput@0x06001234
Mode: around/trampoline
Handler: count-calls
```

After one input validation in the target, `events watch` and `hook stats`
observe the same counter transition:

```console
$ "$IL2BRIDGE" events watch --after 0
1  hook-added  tid=42473  42420:31147059:0  mode=around,transport=trampoline,handler=count-calls
2  hook-hits  tid=42473  42420:31147059:0  delta=1,total=1
^C

$ "$IL2BRIDGE" hook stats '42420:31147059:0'
42420:31147059:0  hits=1

$ "$IL2BRIDGE" hook remove '42420:31147059:0'
Hook 42420:31147059:0 removed.
```

The ID is valid only for that exact process lifetime. `hook list` shows every
active hook, while `hook watch ID` filters the event stream to one hook. Add
`--json` before a command for machine-readable output; continuous JSON watches
use NDJSON.

### 4. Inspect metadata offline

The broker can enumerate IL2CPP metadata version 39 without starting a target:

```bash
$IL2BRIDGE enumerate /path/to/global-metadata.dat > metadata.json
```

The output includes images, types, method names, arities, static flags, and
metadata tokens. Passing an application executable instead is also supported
when the metadata follows the usual Unity directory layout.

## Method and hook model

A metadata token is the authoritative method identity. The readable method
name remains in the selector and in hook listings, but overload resolution does
not depend on it:

```text
Sample.dll!Example.Input.Controller::ValidateInput@0x06001234
```

The legacy-readable form remains useful when no token is available:

```text
Sample.dll!Example.Input.Controller::ValidateInput/1
```

Name/arity resolution enumerates the class and returns `ERR ambiguous` rather
than silently choosing one of several matching methods.

| Mode | Transport | Current behavior |
|---|---|---|
| `around` | `trampoline` | Counts the call, then executes the relocated original body. |
| `replace` | `breakpoint` | Redirects execution to a replacement handler; the original body is not called. |
| `replace` | `trampoline` | Redirects execution to a replacement handler through a native jump. |

The built-in `count-calls` handler supports transparent `around` probes and a
breakpoint-based replacement counter for void methods. Replacement hooks can
change application behavior by design.

## Engineering and security properties

| Concern | Current design |
|---|---|
| Target boundary | All addresses remain inside the loader; IPC exposes bounded integer handles. |
| Client identity | The loader validates `SO_PEERCRED` and accepts only the effective user that owns it. |
| Socket | Private XDG subdirectory, socket mode `0600`, and a controlled `/tmp` fallback. |
| Process identity | Hook IDs include PID and `/proc` start ticks to detect PID reuse. |
| Hot path | Counter probes execute a generated atomic increment and jump; event formatting happens on the IPC thread. |
| Memory bounds | 256 append-only hook slots, 1,024 event slots, bounded names and payloads. |
| Event loss | Overwritten or contended publications increment an observable dropped-event counter. |
| Shutdown | The watcher is joinable and the IPC socket is unlinked; active hooks must be removed before unloading the library. |

## Current limitations

- Hooking is implemented for Linux x86-64 only.
- Runtime discovery currently expects the conventional `GameAssembly.so`
  basename.
- Offline metadata parsing currently targets metadata version 39.
- The native method address is read from the first field of IL2CPP's internal
  `MethodInfo` layout because the public API exposes no equivalent accessor.
- A trampoline needs at least 14 safely relocatable bytes at the method entry.
  Unsupported relative instructions or a decoded patch region larger than the
  saved-byte capacity are rejected with `hook-install-failed`.
- The decoder does not yet know the method's exact native code extent. A method
  shorter than the required patch may therefore let decoding continue into
  adjacent code; this is a known corruption risk, not a guarded case.
- Trampoline installation writes a multi-byte patch that is not atomic with
  respect to other target threads.
- Hook removal does not suspend target threads or wait for in-flight trampoline
  executions before releasing trampoline memory.
- Breakpoint replacement counters currently assume a void return.
- `hook-hits` events are aggregate samples produced by the IPC thread. Their
  timestamp and thread ID describe publication, not the original method call.
- Hook registry slots are not reused during a process lifetime.
- Arbitrary typed `CALL`, argument serialization, exception transport, and
  transactional hook manifests are not implemented.

These constraints are intentional visibility, not hidden compatibility claims.
Several unsupported relocation shapes are rejected before patching, while the
remaining risks above define where the current model still needs hardening.

## Roadmap

Near-term work focuses on:

1. self-describing handler and capability discovery;
2. typed event payloads and safer argument introspection;
3. typed invocation only after thread attachment, execution context, result,
   and exception semantics are defined.

## License

Il2Bridge is available under the [MIT License](LICENSE).
