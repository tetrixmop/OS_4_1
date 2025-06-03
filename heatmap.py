# Построение хитмапы активности страниц из логов.

import re
import glob
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap, BoundaryNorm
def read_logs(file_pattern):
    events = []
    for fname in glob.glob(file_pattern):
        try:
            with open(fname, 'r', encoding='utf-8') as fin:
                for line in fin:
                    m = re.match(r'(\d+):(\d+):\s*(.+)', line.strip())
                    if m:
                        pid = int(m.group(1))
                        ts  = int(m.group(2))
                        act = m.group(3)
                        events.append((pid, ts, act))
        except UnicodeDecodeError:
            # Если не подошла utf-8, пробуем latin-1
            with open(fname, 'r', encoding='latin-1') as fin:
                for line in fin:
                    m = re.match(r'(\d+):(\d+):\s*(.+)', line.strip())
                    if m:
                        pid = int(m.group(1))
                        ts  = int(m.group(2))
                        act = m.group(3)
                        events.append((pid, ts, act))
        except Exception as e:
            print(f"[Heatmap] Не смог прочесть файл {fname}: {e}")

    # Сортируем все события по возрастанию времени
    events.sort(key=lambda x: x[1])
    return events

def collect_page_activity(events_list):

    page_events = {}      # ключ = page, значение = список  (start, end, type)
    running      = {}    # ключ = pid, значение = (page, start_time, type)

    for pid, ts, act in events_list:
        # Если начали либо чтение, либо запись
        if act.startswith("Start_"):
            parts = act.split('_')
            if len(parts) >= 4:
                page_num = int(parts[-1])
                act_type = 'write' if parts[1] == "Write" else 'read'
                running[pid] = (page_num, ts, act_type)

        # Если закончили
        elif act.startswith("End_"):
            parts = act.split('_')
            if len(parts) >= 4 and pid in running:
                page_num = int(parts[-1])
                st_page, st_ts, st_type = running[pid]
                if st_page == page_num:
                    if page_num not in page_events:
                        page_events[page_num] = []
                    page_events[page_num].append((st_ts, ts, st_type))
                    del running[pid]

    return page_events

def build_heatmap_matrix(page_events, forced_time_range=None):
    if not page_events:
        return None, None, None

    all_ts = []
    for evt_list in page_events.values():
        for st, en, _ in evt_list:
            all_ts.extend([st, en])
    if not all_ts:
        return None, None, None

    T_min = min(all_ts)
    T_max = max(all_ts)
    if forced_time_range is not None:
        T_min, T_max = forced_time_range

    max_pg = max(page_events.keys())
    time_bins = np.arange(T_min, T_max + 1, 1)
    page_bins = np.arange(0, max_pg + 2, 1)

    # Матрица: rows = страницы (0..max_pg), cols = дискретные таймы
    hmap = np.zeros((len(page_bins)-1, len(time_bins)-1))

    for pg, recs in page_events.items():
        for st, en, tp in recs:
            i0 = np.searchsorted(time_bins, st) - 1
            i1 = np.searchsorted(time_bins, en) - 1
            if 0 <= pg < hmap.shape[0] and i0 < i1:
                val = 2 if tp == 'write' else 1
                hmap[pg, i0:i1] = val

    return hmap, time_bins, page_bins

def draw_heatmap(hmap_data, t_bins, p_bins, title="Page Activity Heatmap"):
    if hmap_data is None:
        print("[Heatmap] Нет данных для отрисовки.")
        return

    fig, ax = plt.subplots(figsize=(14, 7))

    cmap = ListedColormap(['white', 'blue', 'red'])
    bounds = [0, 0.5, 1.5, 2.5]
    norm  = BoundaryNorm(bounds, cmap.N)

    # pcolormesh: t_bins – ось X, p_bins – ось Y, а данные – heatmap
    im = ax.pcolormesh(t_bins, p_bins, hmap_data, cmap=cmap, norm=norm, shading='auto')

    ax.set_xlabel("Timestamp (ms)")
    ax.set_ylabel("Page Index")
    ax.set_title(title)

    # Колорбар с тиками
    cbar = fig.colorbar(im, ax=ax, ticks=[0.5, 1.5, 2.5])
    cbar.ax.set_yticklabels(['Idle', 'Read', 'Write'])

    plt.tight_layout()
    plt.show()

def main():
    print("[Heatmap] Считываем логи...")
    readers = read_logs("reader*")
    writers = read_logs("writer*")

    print("[Heatmap] Группируем события по страницам (чтение)...")
    read_events  = collect_page_activity(readers)
    print("[Heatmap] Группируем события по страницам (запись)...")
    write_events = collect_page_activity(writers)

    # Объединяем: объединить списки по page
    all_pages = {}
    for pg, recs in read_events.items():
        all_pages.setdefault(pg, []).extend(recs)
    for pg, recs in write_events.items():
        all_pages.setdefault(pg, []).extend(recs)
    for pg in all_pages:
        all_pages[pg].sort(key=lambda x: x[0])

    print("[Heatmap] Формируем матрицу полной активности...")
    full_map, t_bins, p_bins = build_heatmap_matrix(all_pages)
    draw_heatmap(full_map, t_bins, p_bins, "Combined Read/Write Activity")

    if read_events:
        print("[Heatmap] Формируем матрицу «только чтение»...")
        rmap, rt, rp = build_heatmap_matrix(read_events)
        draw_heatmap(rmap, rt, rp, "Read-Only Activity")

    if write_events:
        print("[Heatmap] Формируем матрицу «только запись»...")
        wmap, wt, wp = build_heatmap_matrix(write_events)
        draw_heatmap(wmap, wt, wp, "Write-Only Activity")


if __name__ == "__main__":
    main()
