"""Summarise a Switch log run per scene: FPS, frame time, renderer and guest cost.

Usage: python scripts/analyze-run.py build/logs/dbz3/dev/runNN

Needs the base60-prof style build (LoopMetrics + FrameClockMetrics): reads
t26-loop-prof.log and t21-clock.log. Scenes are told apart by guest task:
0x001DBC58 is the DU map, 0x001D1604 the level screen, the rest duel/menus.
"""
import re, sys, statistics as st

run = sys.argv[1]
kv = re.compile(r'(\w+)=(-?\d+(?:\.\d+)?)')

# --- t26 loop profile: windows of ~5 s
wins = []
cur = None
for line in open(f'{run}/t26-loop-prof.log', errors='replace'):
    if line.startswith('window_ms='):
        cur = dict(kv.findall(line)); cur = {k: float(v) for k, v in cur.items()}
        cur['window_ms'] /= 1000.0   # logged in microseconds
        cur['tasks'] = []; cur['steps'] = None
        wins.append(cur)
    elif cur is not None:
        m = re.search(r'logic60 steps/s=([\d.]+)', line)
        if m: cur['steps'] = float(m.group(1))
        m = re.match(r'\s+task fn=0x([0-9A-F]+) .*?runs/pass=([\d.]+) avg_us=\s*(\d+) max_us=\s*(\d+) gcm_runs=(\d+) gcm_kb/pass=([\d.]+)', line)
        if m:
            cur['tasks'].append((m.group(1), float(m.group(2)), int(m.group(3)), int(m.group(4)), float(m.group(6))))

def scene(w):
    fns = {t[0] for t in w['tasks'] if t[1] > 0}
    if '001DBC58' in fns: return 'DU-MAP'
    if '001D1604' in fns: return 'LEVEL'
    return 'other'

# --- t21 clock: windows of 30 frames
clk = []
for line in open(f'{run}/t21-clock.log', errors='replace'):
    if not line.startswith('frames='): continue
    d = {k: float(v) for k, v in kv.findall(line)}
    clk.append(d)
# cumulative time per clock window
t = 0.0
for d in clk:
    d['t0'] = t; t += d['wall_ms']; d['t1'] = t
print(f'clock windows={len(clk)} total_s={t/1000:.1f} frames={clk[-1]["frames"]:.0f}')

# t26 windows are 5 s apart; first one holds a cumulative boot value, skip it.
tt = 0.0
rows = []
for i, w in enumerate(wins):
    if i == 0: continue
    t0, t1 = tt, tt + w['window_ms']; tt = t1
    rows.append((i, t0, t1, w))
print(f'loop windows={len(rows)} (boot window skipped)')

def clock_between(t0, t1):
    sel = [d for d in clk if d['t0'] >= t0 - 1 and d['t1'] <= t1 + 1]
    return sel

print('\nidx  scene   passes/s  pass_ms  logic/s  | fps(clk) frame_ms fifo_ms/f sleep_ms/f present_ms/f ppu_cpu_ms/f draws/f')
by_scene = {}
for i, t0, t1, w in rows:
    sc = scene(w)
    sel = clock_between(t0, t1)
    secs = w['window_ms'] / 1000
    pps = w['passes'] / secs
    if sel:
        fr = sum(30 for _ in sel)
        wall = sum(d['wall_ms'] for d in sel)
        fps = fr / (wall / 1000)
        fifo = sum(d['fifo_ms'] for d in sel) / fr
        slp = sum(d['sleep_ms'] for d in sel) / fr
        prs = sum(d['present_ms'] for d in sel) / fr
        ppu = sum(d.get('ppu_main_cpu_ms', 0) for d in sel) / fr
        drw = sum(d.get('fifo_draw_methods', 0) for d in sel) / fr
        print(f'{i:3d}  {sc:7s} {pps:7.1f} {w["pass_avg_us"]/1000:8.2f} {w["steps"] or 0:8.1f}  | {fps:7.1f} {wall/fr:8.2f} {fifo:8.2f} {slp:9.2f} {prs:12.2f} {ppu:12.2f} {drw:8.0f}')
        by_scene.setdefault(sc, []).append((fps, wall / fr, fifo, slp, prs, ppu, drw, pps, w['pass_avg_us'] / 1000, w['steps'] or 0))
    else:
        print(f'{i:3d}  {sc:7s} {pps:7.1f} {w["pass_avg_us"]/1000:8.2f} {w["steps"] or 0:8.1f}  | (no clock windows)')

print('\n=== per scene (mean over windows) ===')
names = ['fps', 'frame_ms', 'fifo_ms', 'sleep_ms', 'present_ms', 'ppu_ms', 'draws/f', 'passes/s', 'pass_ms', 'logic/s']
for sc, v in by_scene.items():
    cols = list(zip(*v))
    print(sc, f'n={len(v)}', ' '.join(f'{n}={st.mean(c):.2f}' for n, c in zip(names, cols)),
          f'fps_min={min(cols[0]):.1f} fps_max={max(cols[0]):.1f}')
