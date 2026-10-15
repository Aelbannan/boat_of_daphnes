# Guest machine state

## Contents

- [Responsibility](#responsibility)
- [Mechanical contents](#mechanical-contents)
- [Ownership and address model](#ownership-and-address-model)
  - [Guest-to-host address resolution](#guest-to-host-address-resolution)
- [Boot state](#boot-state)
- [DOL and REL roles](#dol-and-rel-roles)
- [Guest time and exceptions](#guest-time-and-exceptions)

## Responsibility

Guest state is the GameCube-visible machine model shared by the host and the
translated module. It holds CPU registers, memory, addresses, timing counters,
exceptions, and the program counter at which translated execution resumes.

## Mechanical contents

Mechanically, guest state is an authoritative `CPUState` structure plus the
storage reachable through the guest address space. A translated block reads
its input registers from `CPUState`, performs loads and stores through the
runtime memory helpers, writes its results back to the same structure and
memory, charges guest cycles, and leaves the next guest address in `pc`. The
host receives that same state when the block yields; it does not reconstruct
the CPU from native registers.

```mermaid
flowchart TD
    Host["BlueWake host"]
    Block["Translated DOL or REL block"]
    CPU["`CPUState<br/>registers, PC, timing,<br/>exceptions, access hooks`"]
    Resolve{"Guest address<br/>resolution"}
    MEM1["`MEM1 backing<br/>guest RAM bytes`"]
    Alias["`REL aliases<br/>linked data and BSS`"]
    Services["`Compatibility services<br/>MMIO and special accesses`"]

    Host -->|"dispatch"| Block
    Block -->|"yield"| Host
    Host <-->|"service at boundaries"| CPU
    Block <-->|"read and update"| CPU
    CPU -->|"load or store"| Resolve
    Resolve <-->|"ordinary RAM"| MEM1
    Resolve <-->|"linked REL range"| Alias
    Resolve <-->|"callback"| Services
```

| Part | What it contains mechanically | How it is used |
| --- | --- | --- |
| Integer and floating-point context | 32 general-purpose registers, 32 floating-point registers, paired-single lanes, condition and status registers, link and count registers, and quantization registers | Translated instructions read and write these fields as the emulated Gekko register file. [CPU layout](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L127-L145) |
| Execution and exception context | `pc`, machine-state register, save/restore registers, data-address and status registers, pending exception bits, reservation state, and locked-cache metadata | Identifies the next translated block and preserves the architectural information needed to take or resume from an exception. [CPU layout](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L127-L174) |
| Guest-time accounting | Time base, accumulated cycle charge, per-turn cycle budget, and deadline-observation fields | Lets translated work advance GameCube time and return to the host before a device or interrupt deadline. [CPU layout](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L145-L183) |
| MEM1 backing | A byte array reached through `CPUState.ram`; BlueWake provides 32 MiB so deterministic linked REL storage can extend beyond the retail 24 MiB range | Guest loads and stores resolve cached and uncached MEM1 addresses to this storage and preserve big-endian PowerPC byte order. [Storage contract](../../../cmake/composite/module_cpu_contract.h#L23-L79), [memory access](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L197-L294) |
| REL alias storage | Address-to-storage mappings for relocated REL data and BSS whose linked guest addresses must remain stable | Memory helpers consult the alias registry before falling through to ordinary MEM1 or external access. The host and module register the same backing storage. [Alias setup](../../../runtime/host/src/main.c#L7968-L8101) |
| Native access hooks | Function pointers for external reads and writes, special-purpose registers, cache control, instruction fallback, and host calls | Unmapped or hardware addresses leave ordinary RAM resolution through these hooks and reach the appropriate compatibility service. [CPU hooks](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L106-L125), [host setup](../../../runtime/host/src/main.c#L7943-L7948) |

The translated native instructions themselves are not guest state, and the
original DOL and REL files are inputs rather than live state. Compatibility
services also retain their own device-specific state. Their guest-visible
effects enter this machine state through MMIO results, memory writes, timing
updates, callbacks, and pending exceptions.

## Ownership and address model

The module can export aligned `CPUState` and MEM1 storage. The host selects
those exports when present, initializes them, and passes the same CPU pointer
through dispatch, memory, device, and callback paths. No state copy is required
when execution crosses the host/module ABI.

### Guest-to-host address resolution

A guest address is a 32-bit value in the emulated Gekko address space. It is
not normally a host pointer, and BlueWake does not require the operating system
to map memory at that numeric address. The memory helpers translate it to a
pointer inside the BlueWake process on each access. Consequently, the host
virtual address can change between launches because the module loader and OS
choose where native allocations and code reside.

```mermaid
flowchart TD
    Address["32-bit guest address"]
    Use{"How is it used?"}
    Dispatch["Look up guest PC<br/>in dispatch tables"]
    Native["Call native translated<br/>function"]
    Alias{"Registered REL<br/>alias match?"}
    AliasPointer["Alias backing pointer<br/>+ range offset"]
    Mirror["Canonicalize cached or<br/>uncached MEM1 address"]
    RAM{"Inside allocated<br/>MEM1?"}
    RAMPointer["`CPUState.ram<br/>+ MEM1 offset`"]
    Callback["Call external read or<br/>write hook"]
    Service["MMIO or other<br/>compatibility service"]

    Address --> Use
    Use -->|"program counter"| Dispatch --> Native
    Use -->|"load or store"| Alias
    Alias -->|"yes"| AliasPointer
    Alias -->|"no"| Mirror --> RAM
    RAM -->|"yes"| RAMPointer
    RAM -->|"no"| Callback --> Service
```

Address intervals below are half-open: the first address is included and the
end address is excluded.

| Guest range or address kind | Resolution | Host representation |
| --- | --- | --- |
| `0x80000000–0x81800000` | Retail 24 MiB cached MEM1. Subtract `0x80000000` to obtain the byte offset. | If the MEM1 allocation begins at host pointer `B`, guest `0x80300000` becomes `B + 0x00300000`. `B` is chosen at runtime and is not required to equal `0x80000000`. |
| `0x81800000–0x82000000` | BlueWake's extension from retail MEM1 to the 32 MiB backing allocation. It provides space for raw or deterministically linked REL storage. Registered alias ranges take precedence over the contiguous fallback. | Either `B + (address - 0x80000000)` or the matching alias pointer plus its range offset. [Allocation and extension](../../../runtime/host/src/main.c#L7688-L7696) |
| `0xC0000000–0xC2000000` | Uncached mirror of the same 32 MiB guest-memory window. Clearing bit `0x40000000` produces the corresponding `0x80000000–0x82000000` address before normal alias/MEM1 resolution. | The same host byte as the cached form; for example, `0xC0300000` and `0x80300000` both resolve to `B + 0x00300000`. [Memory resolver](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L197-L255) |
| Generated REL data and BSS ranges | The composite metadata supplies exact linked ranges. The resolver checks the alias registry using the original address and then its bit-`0x40000000`-cleared form. These ranges are generated per module rather than assigned one universal interval. | `alias.storage + (address - alias.linked_start)`. Host services and translated code register the same backing pointer, so both observe the same bytes. [Alias API](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L111-L121), [shared registration](../../../runtime/host/src/main.c#L141-L192) |
| Device and special ranges, including the `0xCC00xxxx` MMIO area and `0xC800xxxx` graphics paths | If no RAM or REL alias matches, the memory helper invokes `CPUState.external_read` or `external_write` with the guest address, access width, and value. | A native function call into the host's device router; there is no host-memory pointer corresponding to the MMIO address. [Memory fallback](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/include/core/cpu.h#L271-L363), [read routing](../../../runtime/host/src/main.c#L3581-L3820), [write routing](../../../runtime/host/src/main.c#L4723) |
| DOL or REL code address in `pc`, `lr`, or `ctr` | The generated dispatcher searches guest code ranges and chooses the translated native function. Physical code PCs below `ram_size` are canonicalized to their cached form; supported uncached DOL PCs are likewise canonicalized before a second lookup. | A loader-assigned native function address, used only for the call. The guest-visible `pc`, `lr`, and `ctr` remain 32-bit guest addresses. [Generated dispatch](../../../scripts/generate_composite.py#L630-L675) |

The formulas describe address translation, not a bulk conversion of guest
memory. Host and translated code retain guest addresses in registers and data
structures, resolving a pointer only when they access the referenced bytes.

| Step | Runtime result | Code reference |
| --- | --- | --- |
| Select module-exported or compatibility storage | One authoritative `CPUState` and MEM1 allocation | [Storage selection](../../../runtime/host/src/main.c#L7329-L7343) |
| Install memory and MMIO accessors | Ordinary addresses reach RAM; hardware ranges reach compatibility services | [MMIO callback setup](../../../runtime/host/src/main.c#L7943-L7948) |
| Establish REL aliases | Relocated data uses linked guest addresses while code addresses remain valid for dispatch and guest function pointers | [REL alias setup](../../../runtime/host/src/main.c#L7968-L8101) |

## Boot state

```mermaid
flowchart TD
    DOL["main.dol"]
    REL["`REL metadata
and data
images`"]
    Loader["Image loader"]
    Memory["`MEM1 +
aliases`"]
    OS["OS globals"]
    Devices["Device state"]
    PC["Entry PC"]

    DOL --> Loader --> Memory
    REL --> Memory
    Memory --> OS --> PC
    Devices --> PC
```

| Step | Runtime result | Code reference |
| --- | --- | --- |
| Initialize devices | Reset video, disc, controller, interrupt, audio, DSP, ARAM, SRAM, and card state | [Host initialization](../../../runtime/host/src/main.c#L7550-L7686) |
| Load DOL sections | Initialized data is copied into MEM1 and BSS is cleared | [Host DOL load](../../../runtime/host/src/main.c#L7957-L7960), [RecompCore loader](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/src/loader.c) |
| Create GameCube OS state | Low memory, arenas, stack, clocks, and machine-state registers satisfy guest startup assumptions | [Host boot call](../../../runtime/host/src/main.c#L8102-L8115), [RecompCore boot](https://github.com/chrissotraidis/RecompCore/blob/ee99bbc13c948351f6974c745cad2593a25eacbf/GXRuntime/src/boot.c) |
| Select the entry point | `cpu.pc` is set to the module descriptor's entry address | [Execution start](../../../runtime/host/src/main.c#L8112-L8115) |

## DOL and REL roles

Ahead-of-time translation replaces executable instructions with native
functions, but the original executable formats still define runtime data and
addresses.

- **DOL** — provides the initial section layout and initialized data copied into
  guest memory. Its translated functions live in the native game module.
- **REL** — provides relocatable module layout, data, BSS, and linked guest
  addresses. Its translated functions also live in the native game module.
- **Dispatch metadata** — maps DOL and REL guest code addresses to their native
  translations. See [composite generation](../../../scripts/generate_composite.py#L562-L707).

## Guest time and exceptions

Translated blocks charge guest cycles rather than reading wall time directly.
The cycle domain advances devices to a shared point and chooses the next safe
boundary. Services publish interrupt sources; the host delivers an exception
only where architectural and scheduler state can be preserved.

| Step | Runtime result | Code reference |
| --- | --- | --- |
| Account completed work | All devices observe one absolute guest timeline | [Cycle domain](../../../runtime/host/src/cycle_domain.c) |
| Advance to the next deadline | Video, audio, DSP, disc, and controller state remain ordered | [Device synchronization](../../../runtime/host/src/main.c#L5455-L5474) |
| Publish and deliver interrupts | Guest exception state and handler PC are established at a safe boundary | [Interrupt routing](../../../runtime/host/src/main.c#L3441-L3544), [delivery](../../../runtime/host/src/main.c#L3078-L3234) |

Return to the runtime [high-level view](../Runtime.md#high-level-view).
