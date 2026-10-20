# Progress — Native work-pool test foundation

## Contents

- [Scoped tasks](#scoped-tasks)
- [Evidence](#evidence)
- [Delivery](#delivery)

[Requirements](PRD.md) · [Technical plan](TECH_PLAN.md)

## Scoped tasks

Status: foundation implemented in [#399](https://github.com/chrissotraidis/bluewake/pull/399),
pending upstream review. Existing M1 identifiers are retained; unchecked items
are not claims that the implementation is absent.

- [ ] M1-T01: Review shared native-math CMake registration and preserved flags.
- [ ] M1-T02: Review standalone production-pool tests without renderer dependencies.
- [ ] M1-T03: Verify configurations, participant assertions, timeouts and Linux targets.
- [ ] M1-T04: Confirm applicable Linux and Windows checks on the final reviewed head.

## Evidence

[#399's validation record](https://github.com/chrissotraidis/bluewake/pull/399)
reports standalone, Linux-integrated and ASan/UBSan results; its
[checks](https://github.com/chrissotraidis/bluewake/pull/399/checks) are the source
for current CI status. This documentation revision ran no game or hardware test.

Do not transfer historical passing counts to a different commit or suite.
Record the exact head, commands, selected cases, environment and limitations
when completing a task.

## Delivery

- Review this plan (#397) with foundation #399.
- [#424](https://github.com/chrissotraidis/bluewake/pull/424) consolidates #406–#411, used when needed by the
  first optimization; preserve their source commits and later oracle fixes.
- Defer #412–#417 and #421; they are not a required merge chain.
- Keep #396 and #400 independent.
- Submit each performance change with benchmark-tour and gameplay evidence.

The superseded full roadmap remains recoverable in
[the earlier plan revision](https://github.com/chrissotraidis/bluewake/tree/293736550f548f06f0da37192a31dbeb61648da0/docs/planning/2026-10-18-gamethread-integtests).
This scoped tracker does not mark removed milestones complete.
