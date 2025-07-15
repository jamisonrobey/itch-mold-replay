# ITCH Mold Replay

Replay NASDAQ ITCH binary files over MoldUDP64.

- **WIP** request server underworks

## Requirements

- Linux
- `g++` >= 14
- `cmake` >= 3.20

## Build

```bash
git clone https://github.com/jamisonrobey/itch_mold_replay.git
cd itch_mold_replay
git submodule update --init --recursive
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

## Usage

```
./itch_mold_replay [OPTIONS] itch_file


POSITIONALS:
  itch_file TEXT:FILE REQUIRED
                              NASDAQ ITCH 5.0 binary message file 

OPTIONS:
  -h,     --help              Print this help message and exit 
          --downstream-group TEXT [239.0.0.1]  
                              Downstream group 
          --downstream-port INT:INT in [1025 - 65535] [30000]  
                              Downstream port 
          --ttl INT:INT in [0 - 255] [1]  
                              Downstream TTL 
          --loopback          Enable downstream multicast loopback 
          --request-address TEXT [127.0.0.1]  
                              Request server address 
          --request-port INT:INT in [1025 - 65535] [31000]  
                              Request server port 
          --replay-speed FLOAT:POSITIVE [1]  
                              Downstream replay speed 
          --start-phase ENUM:value in {close->2,open->1,pre->0} OR {2,1,0} [0]  
                              Market phase to start replay (pre, open, close) 
```

## ITCH File

Tested on ITCH 5.0 files available from  
https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/

- Works on any binary ITCH data where each message is preceded by a 2-byte big-endian length field with timestamp at
  offset of 5.


