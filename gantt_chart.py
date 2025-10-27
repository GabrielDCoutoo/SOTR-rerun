import matplotlib
# --- NEW: Force the 'Agg' backend ---
# This is the most reliable backend for saving files.
matplotlib.use('Agg') 
# ---
import pandas as pd
import matplotlib.pyplot as plt
import sys

LOG_FILE = 'gantt_log.csv'
CHART_DURATION_SEC = 0.5

# --- NEW: Simplified Color Logic ---
# We will just cycle through this simple list.
COLOR_LIST = [
    '#E63946', # Red
    '#457B9D', # Blue
    '#F4A261', # Orange
    '#2A9D8F', # Green
    '#1D3557', # Dark Blue
    '#9A8C98', # Grey
    '#FF6347'  # Tomato
]
# ---

def to_milliseconds(sec, nsec):
    return (sec * 1_000) + (nsec / 1_000_000)

def plot_gantt_chart(df):
    
    task_labels = df.groupby('TaskName')['Priority'].first().sort_values(ascending=False)
    
    # --- NEW: Simplified color and y-axis assignment ---
    task_info = {}
    y_labels = []
    color_index = 0
    for i, (name, prio) in enumerate(task_labels.items()):
        task_info[name] = {
            'y_pos': i,
            'color': COLOR_LIST[color_index % len(COLOR_LIST)]
        }
        y_labels.append(f"{name} (Prio {prio})")
        color_index += 1
    # ---

    fig, ax = plt.subplots(figsize=(20, 10))

    for index, task in df.iterrows():
        task_name = task['TaskName']
        y_pos = task_info[task_name]['y_pos']
        color = task_info[task_name]['color']
        
        ax.barh(
            y=y_pos,
            width=task['Duration_ms'],
            left=task['Start_ms'],
            height=0.8,
            color=color, # Use the simple color
            edgecolor='black',
            linewidth=0.8,
            align='center'
        )

    ax.set_yticks(range(len(y_labels)))
    ax.set_yticklabels(y_labels, fontsize=12)
    ax.set_xlabel('Time (milliseconds)', fontsize=14)
    ax.set_title('Task Execution Gantt Chart (Final Test)', fontsize=18) # New Title
    ax.grid(axis='x', linestyle='--', alpha=0.7)
    ax.grid(axis='y', linestyle=':', alpha=0.5)
    ax.invert_yaxis()

    # --- NEW: Final Filename ---
    output_filename = 'rtsounds_gantt_chart_FINAL.png'
    
    plt.savefig(output_filename, dpi=300, bbox_inches='tight')
    print(f"Gantt chart saved to {output_filename}")

def main():
    print("--- Running SIMPLIFIED version of gantt_chart.py ---")
    try:
        df = pd.read_csv(
            LOG_FILE, 
            skiprows=1, 
            header=None,
            names=['Type', 'TaskName', 'Priority', 'StartSec', 'StartNsec', 'EndSec', 'EndNsec']
        )
    except FileNotFoundError:
        print(f"Error: Log file not found at '{LOG_FILE}'")
        sys.exit(1)
    except pd.errors.EmptyDataError:
        print(f"Error: Log file '{LOG_FILE}' is empty.")
        sys.exit(1)

    df = df[df['Type'] == 'GANTT'].copy()
    if df.empty:
        print("Error: No 'GANTT' data found in the log file.")
        sys.exit(1)

    df['Start_ms'] = df.apply(lambda row: to_milliseconds(row['StartSec'], row['StartNsec']), axis=1)
    df['End_ms'] = df.apply(lambda row: to_milliseconds(row['EndSec'], row['EndNsec']), axis=1)
    df['Duration_ms'] = df['End_ms'] - df['Start_ms']

    min_start_time = df['Start_ms'].min()
    df['Start_ms'] -= min_start_time
    df['End_ms'] -= min_start_time
    
    df_chart = df[df['Start_ms'] < (CHART_DURATION_SEC * 1_000)].copy()

    if df_chart.empty:
        print(f"Error: No log data found within the first {CHART_DURATION_SEC} seconds.")
        sys.exit(1)

    print(f"Loaded {len(df_chart)} task executions.")
    
    plot_gantt_chart(df_chart)

if __name__ == "__main__":
    main()