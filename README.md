# Concurrent Welding Order System

A multithreaded C++ system that receives panel orders from customers, queries price lists from producers, and computes the optimal cost to assemble any requested panel size using dynamic programming — all concurrently.

---

## Features

- **Producer / Customer Decoupling:** Producers and customers run independently; the system brokers communication between them without coupling their lifecycles.
- **Concurrent Order Processing:** A configurable worker-thread pool processes orders in parallel, maximising CPU utilisation across large order batches.
- **Optimal DP Solver:** A recursive memoised solver finds the minimum-cost panel assembly by exploring all horizontal and vertical split combinations.
- **Price-List Merging:** When multiple producers supply the same material, price lists are merged automatically — always keeping the cheapest price per panel shape.
- **Backpressure Control:** A bounded order queue (configurable `kMaxQueueSize`) prevents memory runaway when producers outpace workers.
- **Clean Shutdown:** `stop()` drains all in-flight orders, notifies every customer, and joins every thread before returning.

---

## Architecture

```
┌──────────────┐        sendPriceList()         ┌──────────────────┐
│  CProducer   │ ─────────────────────────────▶ │ CWeldingCompany  │
└──────────────┘                                │                  │
                                                │  addPriceList()  │
┌──────────────┐        waitForDemand()         │  ┌────────────┐  │
│  CCustomer   │ ◀────────────────────────────  │  │ Price DB   │  │
│              │        completed()             │  └────────────┘  │
└──────────────┘ ◀────────────────────────────  │                  │
                                                │   Worker Pool    │
                                                │   ┌─┐ ┌─┐ ┌─┐    │
                                                │   │T│ │T│ │T│    │
                                                │   └─┘ └─┘ └─┘    │
                                                └──────────────────┘
```

Each customer gets a dedicated **serving thread** that blocks on `waitForDemand()`. Incoming orders are split into individual `OrderSlot` entries and pushed onto a shared queue. **Worker threads** pick up slots, wait for the relevant material's price list if it hasn't arrived yet, run `seqSolve()`, and — once every slot in a list is done — call `customer->completed()`.

---

## DP Solver

The core algorithm (`seqSolve`) uses top-down memoised recursion.

Given a set of available panels `{(w, h, cost)}` and a welding cost per unit length, it finds the minimum cost to produce a panel of size `W × H`:

```
solve(W, H) = min(
  direct_cost(W, H),                                          // exact panel from price list
  min over x in [1, W/2]:  solve(x, H) + solve(W-x, H) + H × weld,   // horizontal split
  min over y in [1, H/2]:  solve(W, x) + solve(W, H-y) + W × weld    // vertical split
)
```

Results are cached in a `dp[w][h]` map to avoid recomputation. Panel orientations are treated as equivalent (a `3×5` panel satisfies a `5×3` order).

---

## Project Structure

```
.
├── include/
│   └── CWeldingCompany.h        # Public interface and internal types
├── src/
│   ├── CWeldingCompany.cpp      # Implementation
│   └── main.cpp                 # Sample run using bundled test stubs
├── tests/
│   └── test_welding.cpp         # Unit + integration + stress tests
├── common.h                     # Shared interface (CProd, COrder, CProducer, …)
├── sample_tester.h / .cpp       # Provided test stubs (not part of the library)
├── CMakeLists.txt
└── .gitignore
```

---

## Building

**Requirements:** C++20, CMake ≥ 3.16, a POSIX-threads-capable compiler (GCC / Clang).

```bash
cmake -S . -B build
cmake --build build
```

### Run the sample driver

```bash
./build/welding_main
```

### Run the test suite

```bash
./build/run_tests
# or via CTest:
ctest --test-dir build --output-on-failure
```

---

## Tests

The test suite (`tests/test_welding.cpp`) covers:

| # | Category | What is tested |
|---|----------|----------------|
| 1 | Unit – `seqSolve` | Exact panel match |
| 2 | Unit – `seqSolve` | Single horizontal split |
| 3 | Unit – `seqSolve` | Single vertical split |
| 4 | Unit – `seqSolve` | Cheapest path chosen among alternatives |
| 5 | Unit – `seqSolve` | Panel orientation symmetry (3×5 ≡ 5×3) |
| 6 | Unit – `seqSolve` | Impossible order returns `max` |
| 7 | Unit – `seqSolve` | Multi-level recursive split |
| 8 | Unit – `addPriceList` | Merge keeps cheapest price per shape |
| 9 | Integration | Single customer, sync producer, 4 workers |
| 10 | Integration | Multiple customers + producers, price merge |
| 11 | Stress | 500 trivial orders, 8 workers |

---

## Requirements

- C++20 (`std::atomic`, `std::counting_semaphore` headers, concepts)
- CMake 3.16+
- POSIX Threads (Linux / macOS) or Win32 Threads (MSVC)
