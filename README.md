# ITCH Mold Replay
- Implements a [MoldUDP64](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf) server that replays a binary [Nasdaq TotalView-ITCH](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf) file.
- Downstream server multicasts ITCH messages, using the original timestamps for absolute time-based replay pacing.
- Retransmission server for handling client requests for lost or missed messages by sequence number.
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
### Download replay file and build
- **Note** this downloads/extracts (`gzip` required) a large `~7GB` file into `data/`
```bash
## Build
git clone https://github.com/jamisonrobey/itch_mold_replay.git
cd itch_mold_replay
git submodule update --init --recursive
mkdir -p data
wget -O data/01302019.NASDAQ_ITCH50.gz https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/01302019.NASDAQ_ITCH50.gz
gunzip -k data/01302019.NASDAQ_ITCH50.gz  
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
 ```
### CMake flags
- You can pass some flags to the build which can be useful when profiling or debugging:
- `-DDEBUG_NO_NETWORK=On`
  - Compiles without calls to `sendto()` 
- `-DDEBUG_NO_SLEEP=On`
  - Compiles without calls to `this_thread::sleep_until()`
  - Disables the simulated timing for the downstream feed
## Usage
```bash
./itch_mold_replay [OPTIONS] session itch_file


POSITIONALS:
  session TEXT REQUIRED       MoldUDP64 Session 
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
          --retrans-address TEXT [127.0.0.1]  
                              Retransmission server address 
          --retrans-port INT:INT in [1025 - 65535] [31000]  
                              Retransmission server port 
          --replay-speed, --speed FLOAT:POSITIVE [1]  
                              Downstream replay speed 
          --start-phase, --phase, --start ENUM:value in {close->2,open->1,pre->0} OR {2,1,0} [0]  
                              Market phase to start replay (pre, open, close) 
```
### Example Usage
#### Default (multicast from first message, no TTL or loopback w/ `1x` replay speed)
```bash
./itch_mold_replay SESSION001  ../data/12302019.NASDAQ_ITCH50
```
#### Start downstream multicast at market open with a TTL of 4 and loopback enabled w/ `12.5x` replay speed
```bash
./itch_mold_replay SESSION001 ../data/12302019.NASDAQ_ITCH50 --start-phase open --replay-speed 12.5 --ttl 4 --loopback
```
## Other info
### Replay files
- You can get the historic binary data from [emi.nasdaq.com](https://emi.nasdaq.com/)
  - It only works with TotalView ITCH messages because it relies on the 6-byte timestamp (since midnight) found in each message.
### Wireshark
- A handy tool to inspect the output is the ITCH dissectors for Wireshark by the Open Market Initiative 
- Below is a capture of the multicast feed decoded in Wireshark using the [Nasdaq_Nsm_Equities_TotalView_Itch_v5_0_Dissector.lua](https://github.com/Open-Markets-Initiative/wireshark-lua/blob/main/Nasdaq/Nasdaq_Nsm_Equities_TotalView_Itch_v5_0_Dissector.lua):
![img.png](img.png)
### Sim environment
When this server and a client are on the same network or machine, you will observe a reliable connection which is not very useful if you're trying to test handling out-of-order or missing packets with your client. You can use the utility [tc](https://man7.org/linux/man-pages/man8/tc.8.html) to introduce packet loss, reordering, latency jitter etc if desired.
### Blog post
There is a short [write up](https://jamisonrobey.github.io/moldudp64-totalview-itch-replay-server/) about the implementation.
