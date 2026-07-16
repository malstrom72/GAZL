import sys

W = 12         # cyclic working set: in-place accumulators, >> both pools (arm64 6, x64 4)
R = 6          # rounds unrolled in the single-block loop body
N = 2000000    # loop iterations

lines = []
lines.append("; register-pressure kernel (Belady vs LRU). A single-block loop updates %d INDEPENDENT accumulators in" % W)
lines.append("; place, cycled %d rounds. The working set far exceeds the JIT register pool, and each update is a" % R)
lines.append("; read-modify-write, so an eviction costs a dirty spill AND a reload. In this round-robin, LRU keeps the")
lines.append("; just-used (furthest-next) accumulators and evicts the soonest-needed - near-0 reuse - while block-local")
lines.append("; Belady keeps the soon-used ones. Independent chains, so the extra memory traffic is not hidden by ILP.")
lines.append("; Hand-written (not from Impala).")
lines.append("main:\tFUNC")
lines.append(" PARA *2")
lines.append("$acc: LOCi")
lines.append("$i: LOCi")
for k in range(W):
    lines.append("$t%d: LOCi" % k)
for k in range(W):
    lines.append(" MOVi $t%d #%d" % (k, k + 1))
lines.append(" MOVi $i #0")
first = True
for r in range(R):
    for k in range(W):
        label = ".loop:" if first else ""
        lines.append("%s ADDi $t%d $t%d #%d" % (label, k, k, k + 1))       # ti += (k+1): in-place, independent
        first = False
lines.append(" FORi $i #%d @.loop" % N)
lines.append(" MOVi $acc #0")
for k in range(W):
    lines.append(" ADDi $acc $acc $t%d" % k)
lines.append(" MOVi %1 $acc")
lines.append(" CALL ^printInt %0 *2")
lines.append(" RETU")
sys.stdout.write("\n".join(lines) + "\n")
