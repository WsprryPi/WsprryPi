#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the retained probe's clock model without compiling or loading a module."""

from fractions import Fraction
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "tools/rp1_gpclk_dma_probe/rp1_gpclk_dma_probe.c").read_text()

# Execute the real parent guard with read-only, in-memory clock API doubles.
guard = SOURCE[SOURCE.index("static int validate_profile_parent_rate("):
               SOURCE.index("static int rp1_dma_probe(")]
harness = r"""
#include <assert.h>
#include <stddef.h>
#define EINVAL 22
struct clk { struct clk *parent; unsigned long rate; };
static struct clk *clk_get_parent(struct clk *clock) { return clock->parent; }
static unsigned long clk_get_rate(struct clk *clock) { return clock->rate; }
""" + guard + r"""
int main(void)
{
    struct clk legacy = { NULL, 50000000UL };
    struct clk current = { NULL, 200000000UL };
    /* The output can be 50 MHz even when its parent is 200 MHz. */
    struct clk output = { &current, 50000000UL };
    assert(validate_profile_parent_rate(&output) == -EINVAL);
    output.parent = &legacy;
    assert(validate_profile_parent_rate(&output) == 0);
    output.parent = &current;
    assert(validate_profile_parent_rate(&output) == -EINVAL);
    output.parent = NULL;
    assert(validate_profile_parent_rate(&output) == -EINVAL);
    output.parent = &legacy;
    for (unsigned long rate = 0; rate < 50000000UL; rate += 49999999UL) {
        legacy.rate = rate;
        assert(validate_profile_parent_rate(&output) == -EINVAL);
    }
    legacy.rate = 50000001UL;
    assert(validate_profile_parent_rate(&output) == -EINVAL);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="rp1-probe-clock-") as temporary:
    source = Path(temporary) / "guard.c"
    binary = Path(temporary) / "guard"
    source.write_text(harness)
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11",
                    "-Wall", "-Wextra", "-Werror", "-pedantic", str(source),
                    "-o", str(binary)], check=True, timeout=30, cwd=temporary)
    subprocess.run([str(binary)], check=True, timeout=5, cwd=temporary)

# Both real call sites must reject failure before proceeding to clock mutation
# or DMA allocation. A one-time check cannot detect rate-induced reparenting.
probe = SOURCE[SOURCE.index("static int rp1_dma_probe("):
               SOURCE.index("static void rp1_dma_remove(")]
call = "ret = validate_profile_parent_rate(probe->clk);"
assert probe.count(call) == 2
first, second = probe.index(call), probe.rindex(call)
exclusive = probe.index("ret = clk_rate_exclusive_get(probe->clk);")
seed = probe.index("ret = clk_set_rate(probe->clk, 14097098);")
allocation = probe.index("probe->words = dma_alloc_coherent(")
assert first < exclusive < seed < second < allocation
assert "if (ret)\n\t\treturn dev_err_probe(" in probe[first:exclusive]
assert "if (ret)\n\t\tgoto fail;" in probe[second:allocation]

# Preserve the explicit legacy numerical model; it is not a 200 MHz fixture.
table = SOURCE.split("static const struct tone_profile profiles[] = {", 1)[1]
table = table.split("};", 1)[0]
profiles = re.findall(r"\{(\d+),\s*(\d+),\s*(\d+)\}", table)
assert len(profiles) == 4
for index, values in enumerate(profiles):
    lower, upper, low_count = map(int, values)
    assert upper == lower + 1 and lower >> 16 == upper >> 16
    assert 0 < low_count < 66792
    mean = 50_000_000 * 65536 * (
        Fraction(low_count, lower) + Fraction(66792 - low_count, upper)) / 66792
    requested = 14_097_100 + Fraction(2 * index - 3, 2) * Fraction(12000, 8192)
    assert abs(mean - requested) < Fraction(1, 2000)

print("Legacy DMA probe: parent-rate refusal, reparent checks and 50 MHz fixture: PASS")
