# bms-parser-cpp [WIP]

C++ implementation of Be-Music Script parser 

WIP: Parser interface may change. This is quite functional though; you can use it right away.

You can get amalgamated code from [releases](https://github.com/SNURhythm/bms-parser-cpp/releases), or you can build it by yourself by running `make amalgamate` in the root directory.

## Goal
- [ ] Implement blazing-fast parser with parallel processing

## TODOs 
- [ ] Implement client-specific commands like 
  - [x] [#SCROLL](https://bemuse.ninja/project/docs/bms-extensions/#speed-and-scroll-segments)
- [ ] Refactor interface to fit standard conventions
- [ ] Provide note position calculator

## Others

Check [bms-parser-py](https://github.com/SNURhythm/bms-parser-py) for Python implementation

