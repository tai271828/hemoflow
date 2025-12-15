#!/usr/bin/env python3
"""
Generate a simple flowrate.txt with a repeating pattern: long, short, short, short beats.
This makes it easy to identify the pattern in simulation results.
"""
import numpy as np

# Pattern parameters
beat_duration_long = 1.0   # seconds - long beat
beat_duration_short = 0.3  # seconds - short beat
baseline = 0.3             # baseline flow rate (fraction of max)
peak = 1.0                 # peak flow rate

# Number of cycles to generate
num_cycles = 3

# Time resolution (number of points per second)
points_per_second = 50

def create_beat(duration, peak_value, baseline_value, points_per_second):
    """Create a single beat with quick rise, peak, and gradual fall"""
    num_points = int(duration * points_per_second)

    # Rise quickly (20% of beat)
    rise_points = max(2, int(num_points * 0.2))
    # Peak briefly (10% of beat)
    peak_points = max(1, int(num_points * 0.1))
    # Fall gradually (70% of beat)
    fall_points = num_points - rise_points - peak_points

    # Create the beat shape
    rise = np.linspace(baseline_value, peak_value, rise_points)
    peak_region = np.ones(peak_points) * peak_value
    fall = np.linspace(peak_value, baseline_value, fall_points)

    beat = np.concatenate([rise, peak_region, fall])

    return beat

# Build the time series
time = []
values = []

current_time = 0.0
dt = 1.0 / points_per_second

for cycle in range(num_cycles):
    # Long beat
    beat = create_beat(beat_duration_long, peak, baseline, points_per_second)
    for val in beat:
        time.append(current_time)
        values.append(val)
        current_time += dt

    # Three short beats
    for i in range(3):
        beat = create_beat(beat_duration_short, peak * 0.7, baseline, points_per_second)
        for val in beat:
            time.append(current_time)
            values.append(val)
            current_time += dt

# Convert to arrays
time = np.array(time)
values = np.array(values)

# Normalize time to start at 0 and values to have max of 1.0
time = time - time[0]
values = values / values.max()

# Write to file in the required format
output_file = "input/simple_flowrate.txt"

with open(output_file, 'w') as f:
    # First line: number of data points
    f.write(f"{len(time)}\n")

    # Subsequent lines: time value
    for t, v in zip(time, values):
        f.write(f"{t:.6f}\t{v:.6f}\n")

print(f"Generated flowrate file: {output_file}")
print(f"  Number of points: {len(time)}")
print(f"  Time range: {time[0]:.3f} to {time[-1]:.3f} seconds")
print(f"  Value range: {values.min():.3f} to {values.max():.3f}")
print(f"  Pattern: long-short-short-short repeated {num_cycles} times")
print(f"  Cycle period: {(beat_duration_long + 3*beat_duration_short):.3f} seconds")
