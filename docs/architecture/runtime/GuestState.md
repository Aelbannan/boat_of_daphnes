# Guest machine state

## Contents

- [Responsibility](#responsibility)
- [Mechanical contents](#mechanical-contents)
- [Ownership and address model](#ownership-and-address-model)
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
