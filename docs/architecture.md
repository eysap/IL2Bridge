# Il2Bridge architecture

This document describes the engineering model implemented by Il2Bridge today.
It focuses on ownership, lifecycle, concurrency, and failure boundaries rather
than repeating the broker command reference from the main README.

## Design goals

Il2Bridge separates process-local instrumentation from user-facing control:

- keep the injected component small and written against a C ABI;
- never accept a raw target address from an external client;
- make method and hook identities stable enough to survive repeated broker
  invocations;
- keep memory use bounded inside the target;
- preserve the original body for transparent probes when relocation is safe;
- reject unsupported operations instead of guessing;
- keep offline metadata inspection independent from live hooking.

It is not currently a general-purpose debugger, a cross-platform hooking
library, or an arbitrary managed-method invocation runtime.

## Components

```text
target process
├── watcher thread
│   └── /proc/self/maps discovery + RTLD_NOLOAD
├── IL2CPP bridge
│   └── resolved il2cpp_* function table and lookup caches
├── IPC thread
│   ├── command parsing and peer validation
│   ├── opaque image/class/method handle tables
│   ├── serialized hook installation and removal
│   └── counter sampling and event publication
├── hook registry
│   ├── breakpoint entries
│   └── trampoline entries
└── bounded event ring

external process
└── C++ broker
    ├── target discovery and session validation
    ├── canonical method and hook identities
    ├── automatic resolution of loader handles
    ├── human/JSON output
    └── event read/watch polling
```

### Loader

The loader is a shared C library intended to enter the target through
`LD_PRELOAD`. Its constructor only registers the readiness callback and starts
the watcher. It does not assume that IL2CPP is initialized at ELF constructor
time.

The loader owns:

- module discovery;
- runtime symbol resolution;
- all target pointers and executable-memory changes;
- hook and event storage;
- the Unix socket server.

### Broker

The C++20 broker is a short-lived client. It does not retain process pointers or
rely on in-memory state between invocations. Stable target and hook IDs let
separate CLI processes refer to the same loader session.

The broker owns:

- scanning candidate sockets;
- validating loader identity against Unix peer credentials;
- resolving a canonical selector into the loader's opaque handle chain;
- presenting hook lifecycle and event operations;
- human-readable, JSON, and NDJSON output.

### Offline metadata parser

The metadata library parses `global-metadata.dat` version 39 independently from
the live loader. It decodes images, types, methods, flags, arities, and tokens.
The resulting token is reusable in a canonical live selector when the metadata
matches the running application.

## Startup and shutdown

```text
ELF constructor
    │
    ├── register ready callback
    └── start watcher thread
            │
            ├── scan /proc/self/maps for GameAssembly.so
            ├── obtain a handle with RTLD_NOLOAD
            ├── wait for the configurable startup grace period
            └── resolve required il2cpp_* exports
                    │
                    └── start IPC server
```

Discovery is passive: the watcher never loads `GameAssembly.so` and does not
call an IL2CPP initialization routine. Polling backs off from 5 ms to 100 ms
and stops if the module was never observed within a configurable timeout,
adjustable with `IL2BRIDGE_WATCHER_TIMEOUT_MS` (default 60 seconds; `0`
disables the timeout). Once observed, the complete grace period is honored
before publishing readiness. The default is five seconds and can be adjusted
with `IL2BRIDGE_WATCHER_GRACE_MS`.

On a clean unload, the destructor requests watcher shutdown, joins the watcher,
stops and joins the IPC thread, closes its descriptors, and unlinks the socket.
It does not iterate and uninstall active hooks; callers must remove them before
unloading the shared library. Normal process exit does not execute patched code
after address-space teardown, but unloading the loader from a running process
with active detours is unsupported.

## Runtime bridge and method identity

The bridge resolves required symbols once a mapped module handle is available.
Core lookup currently depends on:

- `il2cpp_domain_get`;
- `il2cpp_domain_get_assemblies`;
- `il2cpp_assembly_get_image`;
- `il2cpp_image_get_name`;
- `il2cpp_class_from_name`;
- `il2cpp_class_get_methods`;
- `il2cpp_method_get_name`;
- `il2cpp_method_get_param_count`;
- `il2cpp_method_get_token`.

Object introspection, invocation, string conversion, and thread attachment are
separate optional capability groups. Missing optional exports disable only that
group; missing core exports prevent the IPC server from being published.

Lookup caches are fixed-capacity, mutex-protected tables keyed by exact image
name and length-delimited class/method components. The cache is an optimization:
failure to cache does not change lookup correctness.

### Canonical selectors

The authoritative selector is:

```text
Assembly.dll!Namespace.Type::Method@0x06001234
```

Resolution proceeds as follows:

1. enumerate domain assemblies and match the exact image name;
2. resolve the class by image, namespace, and type name;
3. enumerate class methods and match the metadata token;
4. store the resulting pointers in loader-owned handle tables.

The name/arity form is retained as a readable fallback:

```text
Assembly.dll!Namespace.Type::Method/2
```

It enumerates every method in the class and succeeds only when exactly one
method matches. Multiple matches return `ERR ambiguous`.

### Opaque handles and sessions

The text protocol exposes small integer handles for images, classes, methods,
and hook slots. A `HOOK` command can only reference a method previously placed
in the method table; an external client cannot supply a native address.

The broker hides this chain behind a selector. It identifies a target session
as `<pid>:<start_ticks>`, where start ticks come from `/proc/<pid>/stat`. A hook
ID adds the registry slot: `<pid>:<start_ticks>:<slot>`. This prevents a hook ID
from silently targeting a different process after PID reuse.

## Hooking model

All hook mutations execute on the single IPC thread. This is an architectural
invariant: registry allocation and code patching are single-writer operations.
Target threads and the SIGTRAP handler only perform published read-side lookups.

The registry contains 1,024 append-only slots. Removal atomically disables an
entry, but its slot is not reused during that process lifetime. This makes stale
handles fail predictably and avoids ABA-style slot reuse.

### Breakpoint transport

A breakpoint hook replaces the first target byte with `INT3`. A process-wide
`SIGTRAP` handler locates the corresponding registry entry and redirects the
saved instruction pointer to the detour.

The one-byte patch is naturally atomic on the supported platform, but this
transport is replacement-only: the original instruction stream is not called.
The generated replacement counter increments its state and returns, and is
therefore currently suitable only for methods with a void return contract.

### Trampoline transport

A trampoline hook uses Zydis to decode complete instructions until at least 14
bytes can be replaced by an absolute indirect jump. The copied instructions are
placed in executable memory within approximately ±2 GiB of the target so
32-bit relative operands remain representable. Supported RIP-relative memory
references and external relative branches are relocated before a tail jump
returns to the original body after the patch.

The transport rejects a target when:

- instruction decoding fails;
- the safe patch region exceeds the 32-byte saved-instruction capacity;
- a displacement cannot be represented after relocation;
- a relative branch lands inside the copied patch region;
- executable memory cannot be mapped at a suitable address.

The decoder does not know the native end address of the selected method. If a
method is shorter than the minimum patch, decoding can continue into adjacent
code and a successful-looking installation can corrupt that code. A future
implementation needs a trustworthy native extent or an independent boundary
check before this case can be rejected.

The generated `around` counter stub performs an atomic increment and tail-jumps
to the trampoline. Integer and floating-point arguments are not reformatted or
serialized on this path.

Installation of the 14-byte jump is not atomic with respect to target threads.
This is the most important unresolved concurrency risk in the current hooking
implementation.

Removal has the corresponding lifetime risk: it restores the target bytes and
releases owned trampoline memory without first proving that no target thread is
still executing inside that trampoline. Installation and removal therefore
require an external notion of a safe point that the current protocol does not
provide.

### Native method pointer assumption

IL2CPP's exported API does not provide a native-code-address accessor for a
method. The bridge currently reads the first pointer-sized field of the internal
`MethodInfo` layout as `methodPointer`. This assumption is isolated in one
adapter, documented in code, and remains version-sensitive.

## IPC boundary

The preferred endpoint is:

```text
$XDG_RUNTIME_DIR/il2bridge/<pid>.sock
```

The loader creates or validates a user-owned private directory, applies mode
`0600` to the socket, and falls back to `/tmp/il2bridge-<pid>.sock` when the XDG
runtime path is unavailable. A stale socket is reclaimed only when it is a
socket owned by the current user and no live listener accepts a connection.

Each accepted peer is checked with `SO_PEERCRED`; its effective UID must match
the loader. `INFO` then reports protocol version, PID, UID, process start ticks,
and feature names. The broker compares those values with the kernel-provided
credentials before constructing a session.

The server uses one request per connection and a single accept/dispatch thread.
This intentionally serializes handle-table access and hook mutation. Receive
operations have a two-second timeout, request lines are bounded, and writes use
`MSG_NOSIGNAL` so a disconnected client cannot deliver `SIGPIPE` to the target.

The broker scans both XDG and legacy `/tmp` paths during migration and
deduplicates targets by session identity.

## Embedding the loader

The IPC boundary above is not the only way to consume the loader. A second
shared object preloaded into the same process can link against the loader's
public surface directly and drive it in-process, without a broker or a
socket. That surface is exactly the set of symbols marked
`IL2BRIDGE_LOADER_API`; everything else remains hidden.

Readiness works the same way for an in-process consumer as it does for the
watcher's own internal use: `late_init_add_ready_callback` queues a
subscriber, and if readiness has already fired by the time it registers, the
callback runs before the registration call returns. A late-loaded embedder
therefore never needs to poll for `GameAssembly.so` availability.

`hook_install_trampoline` hands back the callable original-body trampoline
before it patches the target. An embedder that installs a detour meant to
chain to the original can rely on that pointer being valid from the very
first call the detour receives, including a call that races the return from
installation.

Hook registry slots follow the same append-only discipline documented above
regardless of caller: an in-process consumer installs a hook once and gates
its own dispatch rather than removing and reinstalling to change behavior,
since a freed slot is never handed back out.

Diagnostics are redirected the same way for either consumption mode.
`il2bridge_set_log_sink` replaces the destination for the loader's log lines;
this matters in particular for an embedder whose host application has
already taken over file descriptor 2, where the loader's default of writing
to stderr would otherwise be lost or interleaved with unrelated output.

## Event model

The event stream is a fixed ring of 1,024 records. Each record contains:

- a monotonic sequence number;
- a monotonic publication timestamp;
- a hook slot;
- the publishing thread ID;
- a bounded event type and copied payload.

Publication uses atomic reservation and never retains an IL2CPP-owned pointer.
Overwriting old history or losing a contended slot increments the dropped-event
counter. Readers request records after a sequence number and can detect when
their cursor predates the oldest retained record.

Current event types are:

- `hook-added`;
- `hook-removed`;
- `hook-hits`.

Counter detours do not format events, read a clock, allocate memory, or enter
the IPC subsystem. They only increment an atomic counter. Before handling a
broker request, the IPC thread samples those counters and publishes aggregate
`delta` and `total` values. `events watch` polls every 200 ms, which drives this
sampling while a consumer is active.

Consequently, a `hook-hits` timestamp and thread ID identify the sample's
publication, not the original call. Exact per-call context is intentionally
deferred until a calling-convention-safe capture model exists.

## Concurrency summary

| Context | Responsibilities | May block? |
|---|---|---|
| Loader constructor | Register callback and start watcher | Briefly during thread creation |
| Watcher thread | Module discovery and readiness grace | Yes, between polls |
| IPC thread | Protocol, resolution, hook mutation, event sampling | Yes, on bounded client I/O |
| Target threads | Execute original code and generated counter stubs | Counter path does not block |
| SIGTRAP handler | Lookup breakpoint entry and redirect RIP | No allocation or locks |

Published registry reads use compiler atomic builtins so the same data layout is
usable from C and C++ test translation units. The event ring also uses atomic
sequence publication. Lookup caches use a mutex because they are not accessed
from signal context.

## Failure model

The loader aims to keep unsupported work local and explicit:

- missing core symbols prevent IPC startup;
- missing optional symbols disable only their capability group;
- invalid or stale handles return protocol errors;
- ambiguous readable selectors are rejected;
- unsafe trampoline relocation aborts installation before patching;
- registry exhaustion and event loss are bounded and observable;
- clean removal restores saved bytes before disabling and releasing owned
  trampoline state.

This failure model does not make live code patching intrinsically safe. It makes
the current assumptions and refusal points reviewable, testable, and visible to
the broker.

## Verification and delivery

The GitHub Actions CI matrix covers complementary compiler, optimization, and
runtime-checking profiles:

| Profile | Role |
|---|---|
| GCC / Debug | Exercises the normal development configuration. |
| GCC / Release | Continuously validates the profile shipped to users. |
| Clang / Release | Detects compiler-specific assumptions in optimized code. |
| Clang / Debug + ASan/UBSan | Detects invalid memory access, leaks, and selected classes of undefined behavior. |

Every profile builds the same C loader and C++ broker, then runs three CTest
targets: broker-focused tests, loader-focused tests, and an out-of-process
integration test. The integration test preloads the real loader into a fixture
process, waits for delayed `GameAssembly.so` discovery, exercises IPC and the
resolve/hook/unhook lifecycle, and verifies clean watcher and socket shutdown.

Two instrumentation boundaries are explicit in the sanitizer profile. Synthetic
hook targets are kept sanitizer-free and start with a relocatable prologue,
because they model native methods compiled outside the instrumented test
binary. Clang's function-type sanitizer is also disabled: runtime-generated
trampolines and ABI-erased handler calls intentionally have no compiler-emitted
function metadata. AddressSanitizer, leak detection, and the remaining UBSan
checks stay active.

Delivery is a separate tag-triggered workflow. A `v*` tag must be reachable
from `main`; the workflow rebuilds and tests with GCC Release before packaging
the broker, loader, README, architecture document, and license. The resulting
Linux x86-64 archive and SHA-256 checksum are attached to a generated GitHub
Release. Ordinary CI has read-only repository access, while write access is
limited to this release job.
