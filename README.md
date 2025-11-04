# when

Finally, after who and why, we are now in when. when is about time, time is important.

### Build Steps

```bash
# Configure the project
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build the executable
cmake --build build
```

### Usage

```bash
# Run the visualizer with microphone capture
./build/when

# Capture system audio instead (requires BlackHole on macOS)
./build/when --system

# Show developer metrics overlay (RMS, peak, dropped samples)
./build/when --dev
```
