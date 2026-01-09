# mGBA environment for PufferLib
I'm doubtful I'll update this readme frequently.

## Todo
- save_state / load_state functions
- rendering logic
- more reward shaping
- data collection
- default NN for mgba
- better separation of env and emulator logic
- figure out a way to conditionally load env based on rom (possibly from ini?)

## C Functions (mgba.h)
| Function | Description |
|----------|-------------|
| `mgba_init_core()` | Initializes the mGBA emulator core with a ROM file. |
| | |
| `read_mem()` | Reads a byte from Game Boy memory at a given address. |
| | |
| `read_bcd_money()` | Reads the player's money (BCD encoded). |
| | |
| `update_observations()` | Copies the video buffer to the observation array as RGB floats. |
| | |
| `set_keys()` | Sets the controller input keys. |




