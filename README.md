# WeGPU BCMC Data Generator

Convert RAW files to the ZFP block compressed layout used by BCMC, or generate synthetic test data sets in the same format.

## Building

First download and build [ZFP](https://github.com/LLNL/zfp), then run CMake and tell the app where to find ZFP:

```
cmake .. -Dzfp_DIR=<path to ZFP install>/lib/cmake/zfp/
cmake --build . --config Release
```

Then you can run the app to print help and view the options to convert or generate data.

