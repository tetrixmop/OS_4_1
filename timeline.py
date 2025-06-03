# график смены состояний (Waiting -> Writing/Reading -> Finished).

import re
import matplotlib.pyplot as plt
import pandas as pd

file_name = "writer_log_27772.txt"

file_type = ''
if "writer" in file_name:
    file_type = "writer"
elif "reader" in file_name:
    file_type = "reader"

sample_logs = open(file_name).read().split("\n")
sample_logs.pop(-1)


def writer_extract_state(text_line):
    """
    Функция парсит строку и возвращает одно из WAIT, START, FIN
    """
    if "Waiting_For_Chunk" in text_line:
        return "WAIT"
    elif "Start_Write_Chunk" in text_line:
        return "START"
    elif "End_Write_Chunk" in text_line:
        return "FIN"
    else:
        return "UNK"

def reader_extract_state(text_line):
    if "Waiting_For_Chunk" in text_line:
        return "WAIT"
    elif "Start_Read_Chunk" in text_line:
        return "START"
    elif "End_Read_Chunk" in text_line:
        return "FIN"
    else:
        return "UNK"


# Списки для таймштампов и «состояний»
times = []
states = []

if file_type == "writer":
    for ln in sample_logs:
        m = re.match(r"\d+:(\d+):", ln.strip())
        if m:
            t = int(m.group(1))
            times.append(t)
            states.append(writer_extract_state(ln))
else:
    for ln in sample_logs:
        m = re.match(r"\d+:(\d+):", ln.strip())
        if m:
            t = int(m.group(1))
            times.append(t)
            states.append(reader_extract_state(ln))

# Создаём DataFrame схожий с «Time» и «Condition»
df = pd.DataFrame({
    "Timestamp_ms": times,
    "Phase": states
})

# Цветовая палитра для разных фаз
color_map = {
    "WAIT": "lightgray",
    "START": "green",
    "FIN": "red"
}

plt.figure(figsize=(14, 6))

# Для каждого состояния рисуем точки на своей «уровневой» линии
for ph in df["Phase"].unique():
    sub = df[df["Phase"] == ph]
    # y-координата сделана как индекс по типу словаря: WAIT→0, START→1, FIN→2
    lvl = ["WAIT", "START", "FIN"].index(ph)
    plt.scatter(sub["Timestamp_ms"], [lvl]*len(sub), 
                label=ph, 
                color=color_map.get(ph, "black"),
                s=50)

# Рисуем штриховые линии между соседними событиями (для простоты рисуем между точками подряд по x)
for i in range(1, len(df)):
    y0 = ["WAIT", "START", "FIN"].index(df["Phase"].iloc[i-1])
    y1 = ["WAIT", "START", "FIN"].index(df["Phase"].iloc[i])
    x0 = df["Timestamp_ms"].iloc[i-1]
    x1 = df["Timestamp_ms"].iloc[i]
    plt.plot([x0, x1], [y0, y1], color="darkgray", linestyle="--", linewidth=1)

# Настройка осей
plt.yticks([0, 1, 2], ["Waiting", "Writing/Reading", "Finished"])
plt.xlabel("Time (ms)")
plt.title("Timeline of States. File: " + file_name)
plt.grid(axis="x", linestyle="--", alpha=0.7)
plt.legend(loc="upper left")
plt.tight_layout()
plt.show()

