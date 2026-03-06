---
author: Brazil
date: March 2026
title: REALITY (example config)
---

Example reality level configuration for `netmux.conf`.  Enable with
`./configure --enable-realitylvls`.  See `REALITY.SETUP.md` for details.

The `Real` level **must** have a value of `1`.

```
# Format: reality_level <8 char name> <hex-byte-mask> <optional-desc: DESC default>
reality_level Real 1
reality_level Obf1 2
reality_level Obf2 4
reality_level Obf3 8 OBFDESC
reality_level Obf4 16 OBFDESC
reality_level Obf5 32 OBFDESC
reality_level Obf6 64 OBFDESC
reality_level Obf7 128 OBFDESC
reality_level Obf8 256 OBFDESC
reality_level Obf9 512 OBFDESC
reality_level Obf10 1024 OBFDESC
reality_level Umbra 2048 UMBRADESC
reality_level Fae 4096 FAEDESC
reality_level Shadow 8192 SHADOWDESC
reality_level Spy 16384
reality_level Death 32768 DEATHDESC
reality_level All 4294967295
```
