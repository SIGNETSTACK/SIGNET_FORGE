window.BENCHMARK_DATA = {
  "lastUpdate": 1773073868360,
  "repoUrl": "https://github.com/SIGNETSTACK/SIGNET_FORGE",
  "entries": {
    "Benchmark": [
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "423b6988f531b9541a27af0f84819988ec0d63ba",
          "message": "chore: clean gitignore — remove internal file names from public view",
          "timestamp": "2026-03-07T05:44:52Z",
          "tree_id": "75b05871e3e17e683c1646351ffd56136fde1e89",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/423b6988f531b9541a27af0f84819988ec0d63ba"
        },
        "date": 1772862449573,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 530.364,
            "range": "± 25.7428",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 564.727,
            "range": "± 31.2555",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2.55547,
            "range": "± 48.6632",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 9.59912,
            "range": "± 49.4533",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 808.896,
            "range": "± 22.4746",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 113.352,
            "range": "± 20.1557",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 20.8994,
            "range": "± 81.4984",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 17.7569,
            "range": "± 100.197",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 113.668,
            "range": "± 23.4777",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 162.453,
            "range": "± 29.1247",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 370.019,
            "range": "± 68.1627",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.18475,
            "range": "± 196.95",
            "unit": "us",
            "extra": "50 samples\n27 iterations"
          },
          {
            "name": "1000 appends",
            "value": 361.121,
            "range": "± 16.0128",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.95356,
            "range": "± 334.66",
            "unit": "us",
            "extra": "50 samples\n16 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 385.539,
            "range": "± 77.7494",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.55898,
            "range": "± 18.8236",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 279.342,
            "range": "± 12.0037",
            "unit": "ns",
            "extra": "50 samples\n115 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 875.944,
            "range": "± 45.4644",
            "unit": "ns",
            "extra": "50 samples\n37 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 281.894,
            "range": "± 5.45454",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 1.03336,
            "range": "± 86.7154",
            "unit": "us",
            "extra": "50 samples\n31 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 377.124,
            "range": "± 86.8557",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 280.655,
            "range": "± 13.3167",
            "unit": "ns",
            "extra": "50 samples\n115 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 379.523,
            "range": "± 81.7986",
            "unit": "ns",
            "extra": "50 samples\n79 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 391.421,
            "range": "± 71.1625",
            "unit": "ns",
            "extra": "50 samples\n82 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 277.447,
            "range": "± 11.3909",
            "unit": "ns",
            "extra": "50 samples\n116 iterations"
          },
          {
            "name": "delta encode",
            "value": 29.387,
            "range": "± 1.48878",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "delta decode",
            "value": 43.8598,
            "range": "± 2.55099",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 29.2371,
            "range": "± 1.05875",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.218,
            "range": "± 1.13414",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.525,
            "range": "± 2.08939",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 69.8884,
            "range": "± 2.36643",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 30.1875,
            "range": "± 1.30624",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.92783,
            "range": "± 188.135",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 57.4007,
            "range": "± 2.10377",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.2327,
            "range": "± 1.24911",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.75557,
            "range": "± 101.992",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 129.431,
            "range": "± 2.66957",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 129.085,
            "range": "± 499.408",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.89,
            "range": "± 56.2479",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.8838,
            "range": "± 36.3194",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.11603,
            "range": "± 0.500972",
            "unit": "ns",
            "extra": "50 samples\n4432 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 388.358,
            "range": "± 83.0958",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 12.6528,
            "range": "± 699.691",
            "unit": "us",
            "extra": "50 samples\n3 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.8971,
            "range": "± 2.29696",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 23.2171,
            "range": "± 1.4394",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "8a66935e9dd9fbca5a8ecfc24ebc1094d8f0c6a9",
          "message": "docs: enable auto-deploy Doxygen API reference to GitHub Pages",
          "timestamp": "2026-03-07T06:22:02Z",
          "tree_id": "5fd67f5548618f9332fe1fe66a2c13989d5caaca",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/8a66935e9dd9fbca5a8ecfc24ebc1094d8f0c6a9"
        },
        "date": 1772864693996,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 558.04,
            "range": "± 25.4012",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 565.825,
            "range": "± 23.4599",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2634.94,
            "range": "± 26994",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 9202.019999999999,
            "range": "± 52789.200000000004",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 912.253,
            "range": "± 30.5324",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 123.633,
            "range": "± 37.879",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.828,
            "range": "± 349.633",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 20.2044,
            "range": "± 91.0175",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 126.758,
            "range": "± 40.261",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 177.555,
            "range": "± 61.8542",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 337.976,
            "range": "± 67.4353",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.07134,
            "range": "± 193.227",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 329.504,
            "range": "± 14.0282",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.6659,
            "range": "± 303.256",
            "unit": "us",
            "extra": "50 samples\n18 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 353.521,
            "range": "± 68.408",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.30872,
            "range": "± 21.5293",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 237.434,
            "range": "± 12.4387",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 756.923,
            "range": "± 67.3393",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 236.75,
            "range": "± 4.82431",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.7967650000000001,
            "range": "± 0.0318489",
            "unit": "us",
            "extra": "50 samples\n37 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 345.549,
            "range": "± 73.3056",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 235.749,
            "range": "± 9.58685",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 342.869,
            "range": "± 60.923",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 363.31,
            "range": "± 89.4117",
            "unit": "ns",
            "extra": "50 samples\n76 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 247.784,
            "range": "± 13.6696",
            "unit": "ns",
            "extra": "50 samples\n120 iterations"
          },
          {
            "name": "delta encode",
            "value": 32.3574,
            "range": "± 2.17366",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 41.9421,
            "range": "± 1.48681",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 29.1228,
            "range": "± 4.9941",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 31.6023,
            "range": "± 6.61017",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 54.5104,
            "range": "± 1.81057",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 64.6014,
            "range": "± 2.24763",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 32.5593,
            "range": "± 2.12883",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.81012,
            "range": "± 164.326",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 54.2191,
            "range": "± 4.2714",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 30.7268,
            "range": "± 6.81435",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.39447,
            "range": "± 101.391",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 123.55,
            "range": "± 250.153",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 123.162,
            "range": "± 1.01849",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.2486,
            "range": "± 74.138",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.2314,
            "range": "± 83.234",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 5.70245,
            "range": "± 0.29073",
            "unit": "ns",
            "extra": "50 samples\n5096 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 378.498,
            "range": "± 101.576",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.2489,
            "range": "± 1.0553",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.2046,
            "range": "± 2.21776",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 21.7714,
            "range": "± 1.94029",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "b7626eb99dca9f5ae346322ead0d98a2b287fe27",
          "message": "docs: add SIGNETSTACK logo and favicon to Doxygen API reference",
          "timestamp": "2026-03-07T06:38:20Z",
          "tree_id": "4ef69444d5453726e154ca4800506f167272ca02",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/b7626eb99dca9f5ae346322ead0d98a2b287fe27"
        },
        "date": 1772865658370,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 557.59,
            "range": "± 38.863",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 561.491,
            "range": "± 29.8943",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2564.8300000000004,
            "range": "± 32585.700000000004",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 8992.19,
            "range": "± 47918",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 867.722,
            "range": "± 17.3312",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 137.541,
            "range": "± 22.9423",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.256,
            "range": "± 312.526",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.771,
            "range": "± 54.6753",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 123.394,
            "range": "± 12.8504",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 165.198,
            "range": "± 21.8806",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 332.394,
            "range": "± 44.579",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.05476,
            "range": "± 104.396",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 325.267,
            "range": "± 5.76539",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.60019,
            "range": "± 128.163",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 352.236,
            "range": "± 70.7959",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.31287,
            "range": "± 20.6933",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 241.569,
            "range": "± 18.5742",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 754.444,
            "range": "± 43.0039",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 239.944,
            "range": "± 4.5069",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.7851290000000001,
            "range": "± 0.043064200000000004",
            "unit": "us",
            "extra": "50 samples\n38 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 343.989,
            "range": "± 60.1103",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 241.57,
            "range": "± 17.7972",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 341.221,
            "range": "± 50.3148",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 361.098,
            "range": "± 78.2515",
            "unit": "ns",
            "extra": "50 samples\n74 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 237.005,
            "range": "± 9.19893",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.2202,
            "range": "± 3.5988",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 43.0825,
            "range": "± 2.41901",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 27.3896,
            "range": "± 1.65902",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 32.0818,
            "range": "± 7.3423",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 55.8757,
            "range": "± 3.51465",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 65.621,
            "range": "± 3.08941",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.0362,
            "range": "± 2.26544",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.80286,
            "range": "± 169.644",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 55.5256,
            "range": "± 5.41706",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 32.1487,
            "range": "± 8.83311",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.61059,
            "range": "± 1.06228",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 121.952,
            "range": "± 518.46",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 122.073,
            "range": "± 1.2903",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.1905,
            "range": "± 57.7567",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.0058,
            "range": "± 89.533",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 5.46866,
            "range": "± 0.19108",
            "unit": "ns",
            "extra": "50 samples\n5283 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 352.164,
            "range": "± 61.4389",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.5688,
            "range": "± 951.452",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 35.6397,
            "range": "± 1.43889",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 22.3252,
            "range": "± 2.86867",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "72c40327f7f661b8b93cc9f5dd20a08db25cd95e",
          "message": "docs: add WASM Parquet demo to GitHub Pages",
          "timestamp": "2026-03-07T06:44:50Z",
          "tree_id": "1d9ee6552969b1d6b32ea638040a56529ceb379b",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/72c40327f7f661b8b93cc9f5dd20a08db25cd95e"
        },
        "date": 1772866061244,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 556.22,
            "range": "± 20.9392",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 562.457,
            "range": "± 21.6399",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2610.05,
            "range": "± 36205.7",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 9040.19,
            "range": "± 46860.700000000004",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 874.261,
            "range": "± 19.9604",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 129.669,
            "range": "± 30.4663",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.5699,
            "range": "± 110.5",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 20.2347,
            "range": "± 129.49",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 132.062,
            "range": "± 35.0715",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 185.581,
            "range": "± 40.3991",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 335.723,
            "range": "± 60.3277",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.07377,
            "range": "± 180.65",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 326.555,
            "range": "± 7.46803",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.63247,
            "range": "± 263.963",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 355.546,
            "range": "± 88.2266",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.29764,
            "range": "± 37.8619",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 233.788,
            "range": "± 12.9292",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 762.968,
            "range": "± 74.9232",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 234.134,
            "range": "± 4.36065",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.796145,
            "range": "± 0.0358381",
            "unit": "us",
            "extra": "50 samples\n37 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 345.708,
            "range": "± 67.2441",
            "unit": "ns",
            "extra": "50 samples\n81 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 231.735,
            "range": "± 9.36627",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 347.917,
            "range": "± 74.1871",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 363.353,
            "range": "± 84.9904",
            "unit": "ns",
            "extra": "50 samples\n76 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 255.054,
            "range": "± 17.5648",
            "unit": "ns",
            "extra": "50 samples\n118 iterations"
          },
          {
            "name": "delta encode",
            "value": 32.791,
            "range": "± 3.20822",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 41.9582,
            "range": "± 1.55715",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 27.5012,
            "range": "± 2.26109",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.052,
            "range": "± 4.47992",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 54.6752,
            "range": "± 2.99893",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 64.2772,
            "range": "± 2.09801",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 31.5845,
            "range": "± 2.09703",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.76394,
            "range": "± 175.126",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 50.7354,
            "range": "± 1.75674",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.8987,
            "range": "± 5.76175",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.30905,
            "range": "± 94.9107",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 122.386,
            "range": "± 602.799",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 122.286,
            "range": "± 427.053",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.2288,
            "range": "± 77.0773",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.188,
            "range": "± 108.647",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 5.70997,
            "range": "± 0.281488",
            "unit": "ns",
            "extra": "50 samples\n5103 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 360.856,
            "range": "± 43.5522",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.0859,
            "range": "± 1.30179",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 35.5616,
            "range": "± 2.09521",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 21.0489,
            "range": "± 1.68395",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "5d218057b7f5dac13adb2f71795a0bd6b244092d",
          "message": "docs: add WASM Parquet demo to GitHub Pages",
          "timestamp": "2026-03-07T06:51:28Z",
          "tree_id": "4e954aef05d04956f64b386ae9294b12c41cd820",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/5d218057b7f5dac13adb2f71795a0bd6b244092d"
        },
        "date": 1772866457952,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 560.211,
            "range": "± 54.7121",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 576.098,
            "range": "± 59.2724",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2584.23,
            "range": "± 70845.20000000001",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 9013.94,
            "range": "± 138881",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 879.652,
            "range": "± 62.1719",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 145.719,
            "range": "± 31.3331",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.4096,
            "range": "± 110.38",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 20.0076,
            "range": "± 260.802",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 138.56,
            "range": "± 29.0261",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 188.169,
            "range": "± 51.1362",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 337.047,
            "range": "± 57.1987",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.06042,
            "range": "± 165.3",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 326.465,
            "range": "± 7.34525",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.63791,
            "range": "± 245.321",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 351.528,
            "range": "± 71.425",
            "unit": "ns",
            "extra": "50 samples\n74 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.33712,
            "range": "± 88.9874",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 236.898,
            "range": "± 14.7453",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 746.918,
            "range": "± 32.2767",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 235.847,
            "range": "± 3.51369",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.796196,
            "range": "± 0.0346976",
            "unit": "us",
            "extra": "50 samples\n37 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 344.213,
            "range": "± 71.5212",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 235.377,
            "range": "± 12.9171",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 345.704,
            "range": "± 61.8466",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 361.26,
            "range": "± 72.3703",
            "unit": "ns",
            "extra": "50 samples\n71 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 242.385,
            "range": "± 13.2038",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.6596,
            "range": "± 2.76834",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 47.0248,
            "range": "± 3.96918",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 37.5036,
            "range": "± 9.22995",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 38.9831,
            "range": "± 10.3256",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 59.9721,
            "range": "± 6.31898",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 71.6506,
            "range": "± 8.9933",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.5742,
            "range": "± 2.34567",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 3.19291,
            "range": "± 253.033",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 64.3717,
            "range": "± 11.6975",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 38.0904,
            "range": "± 10.2482",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.30896,
            "range": "± 125.656",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 120.598,
            "range": "± 359.503",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 120.691,
            "range": "± 509.22",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.0902,
            "range": "± 142.337",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.0165,
            "range": "± 215.014",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 8.43428,
            "range": "± 3.60471",
            "unit": "ns",
            "extra": "50 samples\n5289 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 379.24,
            "range": "± 81.0001",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.9135,
            "range": "± 1.22575",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.9108,
            "range": "± 2.36642",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 23.061,
            "range": "± 2.46584",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "6254ee2e84bf59625475419092ab6a3e8fd1d186",
          "message": "fix: track wasm/sample.parquet for GitHub Pages demo",
          "timestamp": "2026-03-07T06:59:18Z",
          "tree_id": "1dddc5e6131c0e90470ac9ae8fce44aef94619da",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/6254ee2e84bf59625475419092ab6a3e8fd1d186"
        },
        "date": 1772866924579,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 427.78,
            "range": "± 23.1305",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 433.574,
            "range": "± 24.051",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2057.34,
            "range": "± 47179.200000000004",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 7870.62,
            "range": "± 82552.09999999999",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 690.938,
            "range": "± 26.0841",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 169.836,
            "range": "± 49.2378",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.7483,
            "range": "± 325.013",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.2658,
            "range": "± 172.122",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 165.661,
            "range": "± 42.3944",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 254.688,
            "range": "± 61.8563",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 273.851,
            "range": "± 62.0209",
            "unit": "ns",
            "extra": "50 samples\n69 iterations"
          },
          {
            "name": "append 256B",
            "value": 0.8736480000000001,
            "range": "± 0.173353",
            "unit": "us",
            "extra": "50 samples\n21 iterations"
          },
          {
            "name": "1000 appends",
            "value": 264.231,
            "range": "± 13.4631",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 0.70677,
            "range": "± 0.12316",
            "unit": "us",
            "extra": "50 samples\n26 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 291.904,
            "range": "± 69.649",
            "unit": "ns",
            "extra": "50 samples\n60 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.08592,
            "range": "± 37.7517",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 242.752,
            "range": "± 13.5447",
            "unit": "ns",
            "extra": "50 samples\n81 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 722.084,
            "range": "± 57.9809",
            "unit": "ns",
            "extra": "50 samples\n26 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 225.265,
            "range": "± 4.38636",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.456534,
            "range": "± 0.0307592",
            "unit": "us",
            "extra": "50 samples\n40 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 334.311,
            "range": "± 94.635",
            "unit": "ns",
            "extra": "50 samples\n57 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 225.45,
            "range": "± 8.88781",
            "unit": "ns",
            "extra": "50 samples\n81 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 321.289,
            "range": "± 66.6184",
            "unit": "ns",
            "extra": "50 samples\n58 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 366.868,
            "range": "± 95.0921",
            "unit": "ns",
            "extra": "50 samples\n47 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 282.651,
            "range": "± 20.7604",
            "unit": "ns",
            "extra": "50 samples\n61 iterations"
          },
          {
            "name": "delta encode",
            "value": 28.1296,
            "range": "± 3.35107",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 36.3811,
            "range": "± 4.63693",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 26.7392,
            "range": "± 1.00859",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 26.8359,
            "range": "± 1.01929",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 64.4789,
            "range": "± 1.99787",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 61.9375,
            "range": "± 2.06273",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 28.2405,
            "range": "± 2.63645",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 3.09124,
            "range": "± 380.076",
            "unit": "us",
            "extra": "50 samples\n6 iterations"
          },
          {
            "name": "bss encode",
            "value": 47.2505,
            "range": "± 1.64043",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 26.8077,
            "range": "± 1.00969",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 8.15433,
            "range": "± 136.125",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 138.921,
            "range": "± 201.553",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 138.912,
            "range": "± 612.162",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 13.9635,
            "range": "± 81.4104",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.9046,
            "range": "± 111.32",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 13.7077,
            "range": "± 0.0253542",
            "unit": "ns",
            "extra": "50 samples\n1295 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 1003.3700000000001,
            "range": "± 115357",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 11.2316,
            "range": "± 753.825",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 28.4907,
            "range": "± 3.55582",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 37.9223,
            "range": "± 1.55013",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "37af7c5c08b20591aa07ff71414f0455b65fe352",
          "message": "feat: add DEMO button to Doxygen header linking to WASM demo",
          "timestamp": "2026-03-07T07:10:38Z",
          "tree_id": "819082d12c61886645e0f51ef37d60b267566727",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/37af7c5c08b20591aa07ff71414f0455b65fe352"
        },
        "date": 1772867595526,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 557.36,
            "range": "± 37.167",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 570.935,
            "range": "± 45.5822",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2583.6299999999997,
            "range": "± 56212.7",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 9040.150000000001,
            "range": "± 75687.1",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 876.734,
            "range": "± 37.1761",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 140.719,
            "range": "± 32.9779",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.1736,
            "range": "± 110.86",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.8248,
            "range": "± 125.198",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 130.499,
            "range": "± 26.8313",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 184.548,
            "range": "± 42.5815",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 356.184,
            "range": "± 152.983",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.05579,
            "range": "± 129.432",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 326.324,
            "range": "± 7.92289",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.61681,
            "range": "± 258.489",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 352.544,
            "range": "± 79.2622",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.3196,
            "range": "± 19.7137",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 249.804,
            "range": "± 18.8396",
            "unit": "ns",
            "extra": "50 samples\n120 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 768.062,
            "range": "± 40.7095",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 250.34,
            "range": "± 4.92124",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.782231,
            "range": "± 0.0327132",
            "unit": "us",
            "extra": "50 samples\n38 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 344.868,
            "range": "± 55.5899",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 247.902,
            "range": "± 12.9488",
            "unit": "ns",
            "extra": "50 samples\n120 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 344.817,
            "range": "± 75.2177",
            "unit": "ns",
            "extra": "50 samples\n84 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 366.249,
            "range": "± 74.0271",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 235.037,
            "range": "± 11.9744",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "delta encode",
            "value": 35.0437,
            "range": "± 2.75402",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 43.7715,
            "range": "± 2.49787",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 37.7347,
            "range": "± 9.26901",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 39.2354,
            "range": "± 10.5686",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 57.2234,
            "range": "± 5.65137",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 66.0355,
            "range": "± 4.03195",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.226,
            "range": "± 2.48516",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.95003,
            "range": "± 221.988",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 59.0746,
            "range": "± 8.27502",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 33.2521,
            "range": "± 8.76901",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.30577,
            "range": "± 217.82",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 122.026,
            "range": "± 320.041",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 122.57,
            "range": "± 470.73",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.2755,
            "range": "± 89.1571",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.161,
            "range": "± 77.86",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 5.61418,
            "range": "± 0.285445",
            "unit": "ns",
            "extra": "50 samples\n5159 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 390.247,
            "range": "± 91.7402",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.8416,
            "range": "± 944.936",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.6333,
            "range": "± 2.0201",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 22.5425,
            "range": "± 1.93719",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "c567afa917f9072664a806f222f5ff0365a7c97c",
          "message": "feat: add SIGNETSTACK favicon and logo to WASM demo page",
          "timestamp": "2026-03-07T07:17:19Z",
          "tree_id": "85724c213afac3fad0b5150db13d353a2b9cec76",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/c567afa917f9072664a806f222f5ff0365a7c97c"
        },
        "date": 1772868005068,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 550.783,
            "range": "± 28.926",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 562.016,
            "range": "± 30.1335",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 2571.81,
            "range": "± 42870",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 8926.58,
            "range": "± 54377.299999999996",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 877.667,
            "range": "± 32.7596",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 130.501,
            "range": "± 25.8223",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.2913,
            "range": "± 238.353",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.9111,
            "range": "± 873.388",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 132.58,
            "range": "± 24.6389",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 183.982,
            "range": "± 37.0714",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 333.003,
            "range": "± 48.4007",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.06811,
            "range": "± 161.053",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 324.613,
            "range": "± 6.48503",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.65555,
            "range": "± 254.762",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 352.887,
            "range": "± 69.6063",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.30263,
            "range": "± 18.0214",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 241.146,
            "range": "± 11.8001",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 754.593,
            "range": "± 47.6187",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 238.831,
            "range": "± 4.25719",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.821791,
            "range": "± 0.0451586",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 341.502,
            "range": "± 64.9206",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 238.414,
            "range": "± 11.1226",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 338.321,
            "range": "± 48.2014",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 356.426,
            "range": "± 53.8895",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 244.118,
            "range": "± 14.1665",
            "unit": "ns",
            "extra": "50 samples\n121 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.2012,
            "range": "± 2.65023",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 42.0862,
            "range": "± 1.59692",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 44.4154,
            "range": "± 11.0666",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 34.1586,
            "range": "± 9.00327",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 54.475,
            "range": "± 1.89734",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 66.4914,
            "range": "± 3.77477",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.3665,
            "range": "± 2.54747",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 3.03081,
            "range": "± 261.303",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 59.2125,
            "range": "± 9.37973",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss decode",
            "value": 32.459,
            "range": "± 7.67947",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 9.20634,
            "range": "± 117.648",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 122.246,
            "range": "± 173.514",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 122.029,
            "range": "± 295.921",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.1546,
            "range": "± 65.117",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.1695,
            "range": "± 74.5795",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.74252,
            "range": "± 0.43422",
            "unit": "ns",
            "extra": "50 samples\n4085 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 379.548,
            "range": "± 71.7092",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.4357,
            "range": "± 960.796",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.9761,
            "range": "± 2.09808",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 22.0643,
            "range": "± 2.14445",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "53f5b516b75224d62ecaad13cfea02cb0b9f8740",
          "message": "security: hardening pass #5 (53 fixes) + static audit follow-up (11 fixes)\n\n  Full-scale security audit addressing 64 total vulnerabilities across crypto,\n  encoding, I/O, AI tier, and compliance subsystems.\n\n  Pass #5 (53 fixes):\n  - 8 CRITICAL: constant-time GHASH, GCM counter overflow, RLE resize,\n    dictionary error reporting, BYTE_ARRAY bounds, INT4 sign extension,\n    verify_chain early return\n  - 18 HIGH: secure key zeroing, move-only ciphers, CSPRNG hardening,\n    overflow guards, typed statistics, compliance configurability\n  - 18 MEDIUM: X25519 constant-time, TLV caps, Thrift nesting, Arrow caps,\n    bloom filter seed, writer validation, SHA-256 FIPS vector\n  - 9 LOW: Snappy 64-bit, Thrift errors, GCM IV, mmap pre-validation\n\n  Static audit follow-up (11 fixes):\n  - Page CRC-32 (Parquet spec), mmap parity gaps, reader row_group OOB,\n    Z-Order column count validation, Float16 shift UB + unaligned casts,\n    feature flush data loss, getrandom EINTR, delta zigzag unsigned shift,\n    statistics typed merge, compliance silent skip errors, WAL fsync checks\n\n  29 new tests (394 -> 423). Standards: NIST SP 800-38D, FIPS 180-4,\n  RFC 7748, MiFID II RTS 24, EU AI Act Art.12/13/19.",
          "timestamp": "2026-03-07T22:59:01Z",
          "tree_id": "8b5acf865b7da92843a2451e3774863c4efa795e",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/53f5b516b75224d62ecaad13cfea02cb0b9f8740"
        },
        "date": 1772927287802,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 759.576,
            "range": "± 16.4756",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 769.467,
            "range": "± 38.3257",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3689.2,
            "range": "± 77317.7",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 13158.300000000001,
            "range": "± 264702",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 889.31,
            "range": "± 20.5723",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 142.866,
            "range": "± 26.5322",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.4209,
            "range": "± 135.455",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.7259,
            "range": "± 110.382",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 126.951,
            "range": "± 26.4976",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 174.9,
            "range": "± 11.9649",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 331.547,
            "range": "± 43.7556",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.05636,
            "range": "± 119.976",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 329.562,
            "range": "± 11.8884",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.6208,
            "range": "± 212.157",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 345.167,
            "range": "± 41.6918",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.27689,
            "range": "± 28.9205",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 251.377,
            "range": "± 17.1215",
            "unit": "ns",
            "extra": "50 samples\n118 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 760.299,
            "range": "± 32.2839",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 248.15,
            "range": "± 4.79164",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.815701,
            "range": "± 0.0496125",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 342.023,
            "range": "± 61.5447",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 248.066,
            "range": "± 15.3133",
            "unit": "ns",
            "extra": "50 samples\n120 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 340.762,
            "range": "± 49.2461",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 364.705,
            "range": "± 83.5914",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 234.842,
            "range": "± 9.13201",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.332,
            "range": "± 2.55843",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 42.5459,
            "range": "± 1.87112",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 30.7469,
            "range": "± 5.63802",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 30.7072,
            "range": "± 6.41172",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.6165,
            "range": "± 2.17558",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 70.0194,
            "range": "± 3.76881",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 35.9269,
            "range": "± 2.22931",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.91875,
            "range": "± 246.376",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 30.4576,
            "range": "± 5.44346",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 54.2281,
            "range": "± 4.6606",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1701,
            "range": "± 115.397",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 132.057,
            "range": "± 587.291",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 132.353,
            "range": "± 339.459",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 13.2602,
            "range": "± 165.63",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.2765,
            "range": "± 115.906",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 5.45568,
            "range": "± 0.191147",
            "unit": "ns",
            "extra": "50 samples\n5290 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 373.993,
            "range": "± 61.629",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.7068,
            "range": "± 880.169",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 33.7765,
            "range": "± 1.87073",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 21.221,
            "range": "± 1.56405",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "c64b8a5ce5a37ff1c69dae2b9bee4157148880bf",
          "message": "docs: changelog, QA, and security docs for 67-gap compliance milestone\n\n- CHANGELOG: 8 gap fix passes with per-gap regulatory citations\n- QA: 554 tests, 9 fuzz harnesses, compliance gap tracking\n- Security audit documentation updates>",
          "timestamp": "2026-03-09T05:23:16Z",
          "tree_id": "af5778b8bdff0a560400d547fb8957dc717351b9",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/c64b8a5ce5a37ff1c69dae2b9bee4157148880bf"
        },
        "date": 1773033997339,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 753.624,
            "range": "± 27.5711",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 773.736,
            "range": "± 34.1239",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3699.84,
            "range": "± 76317.2",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 12944.4,
            "range": "± 59581.5",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 915.415,
            "range": "± 149.281",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 135.496,
            "range": "± 25.3008",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.7646,
            "range": "± 139.593",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.7952,
            "range": "± 2.85557",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 120.93,
            "range": "± 21.7847",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 180.991,
            "range": "± 33.5599",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 335.588,
            "range": "± 51.775",
            "unit": "ns",
            "extra": "50 samples\n90 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.13158,
            "range": "± 482.076",
            "unit": "us",
            "extra": "50 samples\n29 iterations"
          },
          {
            "name": "1000 appends",
            "value": 329.218,
            "range": "± 11.8517",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.62084,
            "range": "± 239.621",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 357.406,
            "range": "± 79.5757",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.41778,
            "range": "± 451.588",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 235.093,
            "range": "± 9.62835",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 752.321,
            "range": "± 50.9333",
            "unit": "ns",
            "extra": "50 samples\n40 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 236.475,
            "range": "± 5.41164",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.798178,
            "range": "± 0.054055",
            "unit": "us",
            "extra": "50 samples\n38 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 339.479,
            "range": "± 43.339",
            "unit": "ns",
            "extra": "50 samples\n88 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 240.507,
            "range": "± 14.5442",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 340.398,
            "range": "± 53.2317",
            "unit": "ns",
            "extra": "50 samples\n88 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 362.07,
            "range": "± 71.4332",
            "unit": "ns",
            "extra": "50 samples\n74 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 235.33,
            "range": "± 8.74078",
            "unit": "ns",
            "extra": "50 samples\n126 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.5733,
            "range": "± 2.22545",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 42.4004,
            "range": "± 2.13426",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 29.8768,
            "range": "± 5.9445",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 57.0587,
            "range": "± 7.00307",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 53.3869,
            "range": "± 4.18179",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 65.4514,
            "range": "± 2.77453",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.84,
            "range": "± 2.17115",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.85189,
            "range": "± 172.621",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 31.4613,
            "range": "± 6.46631",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 33.2241,
            "range": "± 7.58713",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1728,
            "range": "± 144.815",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.498,
            "range": "± 326.461",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 124.374,
            "range": "± 538.412",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.4623,
            "range": "± 56.0896",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.4755,
            "range": "± 70.1278",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.02474,
            "range": "± 0.266673",
            "unit": "ns",
            "extra": "50 samples\n4196 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 379.944,
            "range": "± 54.9007",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.0697,
            "range": "± 2.41316",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 37.0499,
            "range": "± 1.82086",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 25.3603,
            "range": "± 2.17824",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "25b80c6d19231ed16a15499e686a9dbe9f00cf89",
          "message": "feat: SAST, SBOM, HKDF, human oversight, log retention, fuzz harnesses\n\nNew files addressing 6 enterprise compliance gaps:\n- T-1: CodeQL SAST workflow (Apache 2.0)\n- T-2: CycloneDX SBOM workflow (Apache 2.0)\n- T-3: Crypto fuzz harnesses for AES-GCM, key metadata, PME (Apache 2.0)\n- C-7/C-8: HKDF RFC 5869 key derivation (Apache 2.0)\n- R-1: Log retention manager, EU AI Act Art.12 + MiFID II RTS 24 (BSL 1.1)\n- R-3: Human oversight, EU AI Act Art.14 stop-button + override logging (BSL 1.1)",
          "timestamp": "2026-03-09T05:39:05Z",
          "tree_id": "4e3e504d2f369f8d0ef5bf044e5b4a712394ffe7",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/25b80c6d19231ed16a15499e686a9dbe9f00cf89"
        },
        "date": 1773035357866,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 755.698,
            "range": "± 25.6227",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 765.128,
            "range": "± 30.5386",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3693.85,
            "range": "± 122986",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 12960.5,
            "range": "± 62939.799999999996",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 888.997,
            "range": "± 20.4551",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 122.14,
            "range": "± 21.4393",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.7238,
            "range": "± 366.288",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.2457,
            "range": "± 81.1018",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 137.257,
            "range": "± 25.7107",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 183.212,
            "range": "± 30.1837",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 335.909,
            "range": "± 57.2415",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.06222,
            "range": "± 156.142",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 327.432,
            "range": "± 11.9073",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.63661,
            "range": "± 247.497",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 355.964,
            "range": "± 66.5672",
            "unit": "ns",
            "extra": "50 samples\n76 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.29121,
            "range": "± 17.5796",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 244.45,
            "range": "± 15.5917",
            "unit": "ns",
            "extra": "50 samples\n121 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 759.562,
            "range": "± 42.6078",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 241.031,
            "range": "± 3.94096",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.818088,
            "range": "± 0.0334607",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 345.034,
            "range": "± 68.1661",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 243.882,
            "range": "± 12.3335",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 344.291,
            "range": "± 63.0005",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 363.958,
            "range": "± 85.6399",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 237.726,
            "range": "± 9.34342",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "delta encode",
            "value": 35.7208,
            "range": "± 2.24917",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 43.1409,
            "range": "± 3.10653",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 31.3184,
            "range": "± 5.68431",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 54.6962,
            "range": "± 4.55219",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.4296,
            "range": "± 1.65757",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 65.2365,
            "range": "± 2.43902",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.7467,
            "range": "± 2.90366",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.79823,
            "range": "± 169.159",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 30.1525,
            "range": "± 6.33834",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 31.6811,
            "range": "± 7.45614",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1759,
            "range": "± 171.05",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.98,
            "range": "± 2.91051",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 125.026,
            "range": "± 3.37462",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.5343,
            "range": "± 357.302",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.2883,
            "range": "± 48.615",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.13951,
            "range": "± 0.264075",
            "unit": "ns",
            "extra": "50 samples\n4057 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 367.169,
            "range": "± 85.094",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.6269,
            "range": "± 960.707",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.5914,
            "range": "± 1.91144",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 25.7749,
            "range": "± 2.43373",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "72c2d6b8817e71175197112fe41588714cac67b5",
          "message": "fix: resolve 3 CI failures — MSVC min/max macros, Emscripten mlock, ASan TempDir leak\n\n- Parenthesize std::min/std::max/std::numeric_limits<T>::max() calls across\n  streaming_sink.hpp, feature_reader.hpp, compliance_types.hpp,\n  mifid2_reporter.hpp, eu_ai_act_reporter.hpp to prevent Windows <windows.h>\n  macro pollution (MSVC build failure)\n- Add #elif defined(__EMSCRIPTEN__) no-op branches for mlock/munlock in\n  cipher_interface.hpp (WASM/Docs build failure)\n- Replace fs::path operator/ with string concatenation in TempDir constructor\n  and delete copy ops in test_audit_chain.cpp (ASan 200-byte leak)",
          "timestamp": "2026-03-09T07:47:14Z",
          "tree_id": "a43dbdbac6418975859203121683cbb46a57a6fd",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/72c2d6b8817e71175197112fe41588714cac67b5"
        },
        "date": 1773042588095,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 758.289,
            "range": "± 37.2149",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 776.594,
            "range": "± 61.4125",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3743.44,
            "range": "± 68856",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 13063.800000000001,
            "range": "± 67500",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 897.026,
            "range": "± 24.0552",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 123.951,
            "range": "± 36.6006",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.9316,
            "range": "± 164.953",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.4186,
            "range": "± 162.594",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 117.034,
            "range": "± 31.9975",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 166.718,
            "range": "± 49.4754",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 335.717,
            "range": "± 71.3515",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.06067,
            "range": "± 189.59",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 328.041,
            "range": "± 14.5183",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.64815,
            "range": "± 281.709",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 377.62,
            "range": "± 198.715",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.30719,
            "range": "± 21.8891",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 233.25,
            "range": "± 10.9969",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 750.133,
            "range": "± 31.1442",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 234.192,
            "range": "± 4.68849",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.823362,
            "range": "± 0.0706718",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 345.981,
            "range": "± 69.9267",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 238.365,
            "range": "± 15.1213",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 347.066,
            "range": "± 74.3782",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 363.528,
            "range": "± 83.4825",
            "unit": "ns",
            "extra": "50 samples\n76 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 240.668,
            "range": "± 15.1967",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "delta encode",
            "value": 37.9449,
            "range": "± 2.60263",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 46.5515,
            "range": "± 5.00981",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 37.3942,
            "range": "± 9.66613",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 63.7349,
            "range": "± 10.9008",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 57.0427,
            "range": "± 6.31026",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 67.1579,
            "range": "± 3.90292",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 37.8303,
            "range": "± 3.11584",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 3.15946,
            "range": "± 357.19",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 34.1891,
            "range": "± 9.23503",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 37.4501,
            "range": "± 10.0967",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.3549,
            "range": "± 140.036",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.966,
            "range": "± 543.482",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 125.955,
            "range": "± 2.35935",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.6127,
            "range": "± 145.731",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.5881,
            "range": "± 87.9254",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.04606,
            "range": "± 0.370503",
            "unit": "ns",
            "extra": "50 samples\n4122 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 375.499,
            "range": "± 38.792",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 27.1409,
            "range": "± 883.641",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 37.7908,
            "range": "± 2.3046",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 26.2729,
            "range": "± 3.236",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "b4ef3359629c59fd6952f7d35666b6a26b1c10a6",
          "message": "Gap Fix Pass 10: 7 compliance gaps (C-10/C-17/T-9/T-14/T-15d/T-16/T-17) + CI fixes\n\n- C-10: constant-time fe_inv addition chain (CWE-208 branch-free X25519)\n- C-14: AES-256-only design decision comment in aes_core.hpp\n- C-17: FIPS 140-3 Section 7.1 operator security policy document\n- T-9: property-based testing (8 generative tests, Catch2 GENERATE)\n- T-14: FetchContent SHA-256 hash pinning (supply chain hardening)\n- T-15d: gitleaks secrets scanning CI job\n- T-16: bandit Python SAST CI job\n- T-17: license compliance scanner CI job (licensee + SPDX check)\n- CI fix: MSVC std::numeric_limits parenthesization in test_z_order.cpp\n- CI fix: LSan suppression for libstdc++ path::_M_split_cmpts leak\n- C-16/C-18/T-5/T-18: encryption test additions (Wycheproof vectors)",
          "timestamp": "2026-03-09T08:54:15Z",
          "tree_id": "60951e0a84d92eab0050e4d663ec79e951ccdbd6",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/b4ef3359629c59fd6952f7d35666b6a26b1c10a6"
        },
        "date": 1773046649712,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 765.302,
            "range": "± 52.3119",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 779.292,
            "range": "± 51.1077",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3691.9,
            "range": "± 78405.6",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 13009.4,
            "range": "± 71222.09999999999",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 901.366,
            "range": "± 64.1294",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 168.924,
            "range": "± 30.6986",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.834,
            "range": "± 199.944",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.3025,
            "range": "± 90.8385",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 130.894,
            "range": "± 27.3516",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 206.401,
            "range": "± 38.3593",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 336.196,
            "range": "± 56.0891",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.06551,
            "range": "± 167.619",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 329.555,
            "range": "± 19.0917",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.62713,
            "range": "± 270.972",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 353.028,
            "range": "± 71.6092",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.31115,
            "range": "± 17.9984",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 237.816,
            "range": "± 15.2573",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 754.761,
            "range": "± 48.489",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 238.154,
            "range": "± 3.98735",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.855309,
            "range": "± 0.050822200000000005",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 343.841,
            "range": "± 73.6405",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 238.589,
            "range": "± 12.91",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 346.285,
            "range": "± 67.778",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 366.321,
            "range": "± 70.919",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 241.224,
            "range": "± 14.5649",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "delta encode",
            "value": 40.3858,
            "range": "± 4.6298",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 46.1761,
            "range": "± 5.11323",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 36.9973,
            "range": "± 9.87432",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 70.9957,
            "range": "± 14.0079",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 56.9203,
            "range": "± 7.00928",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 70.2958,
            "range": "± 8.94256",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 39.9142,
            "range": "± 3.91119",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 3.29096,
            "range": "± 539.529",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 42.3206,
            "range": "± 10.5547",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 43.2148,
            "range": "± 11.2312",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.7036,
            "range": "± 268.51",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.763,
            "range": "± 705.208",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 125.282,
            "range": "± 650.506",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.5137,
            "range": "± 111.787",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.3806,
            "range": "± 142.387",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.11033,
            "range": "± 0.360688",
            "unit": "ns",
            "extra": "50 samples\n4103 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 382.737,
            "range": "± 83.8867",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 27.1347,
            "range": "± 1.80721",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 39.2871,
            "range": "± 2.3327",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 28.02,
            "range": "± 4.31379",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "bccf625c7bc641ea197358868ca58f87661df751",
          "message": "fix: resolve 6 CI failures — fe_inv addition chain, MSVC cipher_interface, gitleaks, ASan suppressions\n\n- Fix fe_inv constant-time addition chain: 2 squarings (z^8) not 3 (z^16) per NaCl ref10\n- Parenthesize std::min/std::numeric_limits in cipher_interface.hpp for MSVC\n- Replace gitleaks-action@v2 (requires org license) with free gitleaks CLI binary\n- Remove invalid LSan suppression path from ASAN_OPTIONS (LSan format != ASan format)",
          "timestamp": "2026-03-09T09:18:23Z",
          "tree_id": "84a1e3deb9d914bfa13bb78bd0ad472b8f066744",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/bccf625c7bc641ea197358868ca58f87661df751"
        },
        "date": 1773048064532,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 751.597,
            "range": "± 18.1316",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 778.149,
            "range": "± 66.2126",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3695.86,
            "range": "± 59507.6",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 13000.5,
            "range": "± 53096.5",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 890.602,
            "range": "± 21.7072",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 133.194,
            "range": "± 26.985",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 23.1686,
            "range": "± 318.931",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.7192,
            "range": "± 261.79",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 131.503,
            "range": "± 29.1728",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 187.591,
            "range": "± 50.3385",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 333.38,
            "range": "± 51.5082",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.13498,
            "range": "± 500.091",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 327.536,
            "range": "± 13.2759",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.62884,
            "range": "± 249.64",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 355.936,
            "range": "± 80.613",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.29431,
            "range": "± 16.54",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 243.737,
            "range": "± 9.07336",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 752.694,
            "range": "± 29.005",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 244.044,
            "range": "± 4.60395",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.815183,
            "range": "± 0.047074399999999995",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 339.047,
            "range": "± 44.794",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 246.583,
            "range": "± 19.2382",
            "unit": "ns",
            "extra": "50 samples\n121 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 342.489,
            "range": "± 60.3872",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 384.748,
            "range": "± 89.9973",
            "unit": "ns",
            "extra": "50 samples\n74 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 238.305,
            "range": "± 12.4842",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.7643,
            "range": "± 2.04447",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 41.6204,
            "range": "± 1.53538",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 30.82,
            "range": "± 6.65958",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 56.6312,
            "range": "± 7.16352",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.8406,
            "range": "± 1.58918",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 64.3889,
            "range": "± 2.09494",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.3823,
            "range": "± 2.28963",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.77663,
            "range": "± 171.344",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 28.8968,
            "range": "± 4.67887",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.9996,
            "range": "± 4.83632",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1676,
            "range": "± 128.019",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 125.802,
            "range": "± 370.019",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 125.804,
            "range": "± 270.146",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.5976,
            "range": "± 61.3786",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.6648,
            "range": "± 96.3348",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.26391,
            "range": "± 0.644298",
            "unit": "ns",
            "extra": "50 samples\n4084 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 373.081,
            "range": "± 72.1702",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.5428,
            "range": "± 818.855",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 39.2014,
            "range": "± 2.06329",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 25.3521,
            "range": "± 2.07176",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "74c76cb98ddf1a15a4b1019f7a3321c8e2285ce6",
          "message": "Gap Fix Pass 11: 7 gaps (C-5/P-7/T-6/T-12/T-13/T-15c/T-19) + CI fixes\n\n- C-5: AES-NI/ARMv8-CE hardware detection infrastructure (has_hardware_aes())\n- P-7: AES-128 interop detection in PME with clear error messages\n- T-6: fuzz_hkdf + fuzz_x25519 harnesses, CI runs all 11 fuzz targets\n- T-12: Mull mutation testing baseline CI job\n- T-13: benchmark fail-on-alert enforcement (120% regression gate)\n- T-15c: MSVC /analyze static analysis CI step\n- T-19: API compatibility test suite (19 tests, enum/struct/API stability)\n- Fix fe_inv final multiply: t0 (z^11) not t2 (z^31) for z^(p-2) correctness\n- Fix MSVC min/max macro pollution across 14 headers (comprehensive sweep)\n- Fix gitleaks false positives via .gitleaks.toml allowlist",
          "timestamp": "2026-03-09T09:49:49Z",
          "tree_id": "3a33742293b5b4cdc2647c74f29302c4b17e6a7a",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/74c76cb98ddf1a15a4b1019f7a3321c8e2285ce6"
        },
        "date": 1773049951891,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 758.252,
            "range": "± 36.1972",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 844.357,
            "range": "± 154.489",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3724.4700000000003,
            "range": "± 40046.6",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 13006.9,
            "range": "± 84844.3",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 897.337,
            "range": "± 18.2941",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 141.868,
            "range": "± 28.5927",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.7297,
            "range": "± 102.238",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.3358,
            "range": "± 114.44",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 114.225,
            "range": "± 27.2708",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 182.437,
            "range": "± 33.5356",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 334.48,
            "range": "± 53.7121",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.09756,
            "range": "± 192.615",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 326.718,
            "range": "± 6.85894",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.6192,
            "range": "± 215.578",
            "unit": "us",
            "extra": "50 samples\n18 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 355.51,
            "range": "± 71.3423",
            "unit": "ns",
            "extra": "50 samples\n79 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.33391,
            "range": "± 38.4358",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 280.343,
            "range": "± 33.2412",
            "unit": "ns",
            "extra": "50 samples\n126 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 741.19,
            "range": "± 29.7631",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 230.627,
            "range": "± 3.28678",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.8190259999999999,
            "range": "± 0.0473978",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 344.693,
            "range": "± 64.6126",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 230.245,
            "range": "± 8.7675",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 342.604,
            "range": "± 59.9958",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 358.157,
            "range": "± 63.6616",
            "unit": "ns",
            "extra": "50 samples\n76 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 253.77,
            "range": "± 9.92637",
            "unit": "ns",
            "extra": "50 samples\n117 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.2151,
            "range": "± 2.38164",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 47.728,
            "range": "± 5.47344",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 31.1589,
            "range": "± 6.64293",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 68.2328,
            "range": "± 12.6319",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.7276,
            "range": "± 2.04799",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 64.1889,
            "range": "± 2.02405",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 40.0266,
            "range": "± 3.45237",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.79464,
            "range": "± 165.048",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 31.0236,
            "range": "± 6.7227",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 30.2892,
            "range": "± 6.74185",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1522,
            "range": "± 102.865",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.637,
            "range": "± 451.759",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 123.756,
            "range": "± 388.658",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.4597,
            "range": "± 37.3002",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.3465,
            "range": "± 55.5251",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.18129,
            "range": "± 0.363255",
            "unit": "ns",
            "extra": "50 samples\n4056 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 383.57,
            "range": "± 102.808",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.7933,
            "range": "± 922.699",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 35.9335,
            "range": "± 1.92816",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 24.753,
            "range": "± 1.8819",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "e558efb74820d028bf0f1ab521d07e4961118aa8",
          "message": "fix: resolve 4 CI failures — MSVC TIME_MS macro, fuzz commercial flag, Mull URL, benchmark gate",
          "timestamp": "2026-03-09T10:02:30Z",
          "tree_id": "61d42b0558aa138ad456502c462380c9b6886850",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/e558efb74820d028bf0f1ab521d07e4961118aa8"
        },
        "date": 1773050714748,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 760.794,
            "range": "± 33.4513",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 768.364,
            "range": "± 19.1388",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3683.05,
            "range": "± 17906.4",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 12999.3,
            "range": "± 68382.09999999999",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 887.383,
            "range": "± 17.8709",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 124.937,
            "range": "± 7.29953",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.7106,
            "range": "± 139.579",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.2215,
            "range": "± 64.7945",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 125.221,
            "range": "± 10.8955",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 171.836,
            "range": "± 16.9775",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 330.663,
            "range": "± 38.8613",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.04462,
            "range": "± 91.4752",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 327.303,
            "range": "± 7.47203",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.59666,
            "range": "± 139.36",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 346.169,
            "range": "± 41.9651",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.29286,
            "range": "± 12.9591",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 241.633,
            "range": "± 12.8366",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 753.034,
            "range": "± 28.9678",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 240.197,
            "range": "± 3.32465",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.864383,
            "range": "± 0.0400097",
            "unit": "us",
            "extra": "50 samples\n35 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 340.438,
            "range": "± 56.4974",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 240.28,
            "range": "± 12.5875",
            "unit": "ns",
            "extra": "50 samples\n121 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 339.649,
            "range": "± 41.9059",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 359.198,
            "range": "± 54.7988",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 240.834,
            "range": "± 17.8975",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.4996,
            "range": "± 2.05326",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 41.5105,
            "range": "± 1.64317",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 30.1854,
            "range": "± 6.53299",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 56.634,
            "range": "± 5.92123",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.6523,
            "range": "± 2.19935",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 66.576,
            "range": "± 2.36255",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.7004,
            "range": "± 2.41258",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.81482,
            "range": "± 147.723",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 31,
            "range": "± 6.76146",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 30.7113,
            "range": "± 6.39209",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.129,
            "range": "± 150.664",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.99,
            "range": "± 482.973",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 125.223,
            "range": "± 259.102",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.4767,
            "range": "± 117.148",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.4462,
            "range": "± 55.322",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.25268,
            "range": "± 0.25857",
            "unit": "ns",
            "extra": "50 samples\n3983 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 378.4,
            "range": "± 98.2591",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.599,
            "range": "± 997.721",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.756,
            "range": "± 2.11631",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 25.1513,
            "range": "± 1.93421",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "287533d1bb2a9335f2b3f7db93d7c48a1f976fac",
          "message": "Gap Fix Pass 12: final 5 gaps (D-12/G-9/R-19/R-20/T-8) — 92/92 compliance complete",
          "timestamp": "2026-03-09T10:22:48Z",
          "tree_id": "050f0962e708d39576eed023c7c7287d7fbc5eaf",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/287533d1bb2a9335f2b3f7db93d7c48a1f976fac"
        },
        "date": 1773051915766,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 757.501,
            "range": "± 24.1241",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 788.407,
            "range": "± 23.9919",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3795.4500000000003,
            "range": "± 33771.1",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 14132.300000000001,
            "range": "± 40959.1",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 831.032,
            "range": "± 20.6459",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 118.448,
            "range": "± 24.4563",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 21.0464,
            "range": "± 268.497",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 17.6941,
            "range": "± 265.639",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 117.557,
            "range": "± 24.0424",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 170.053,
            "range": "± 36.259",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 368.293,
            "range": "± 70.6771",
            "unit": "ns",
            "extra": "50 samples\n86 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.2011,
            "range": "± 240.674",
            "unit": "us",
            "extra": "50 samples\n27 iterations"
          },
          {
            "name": "1000 appends",
            "value": 361.921,
            "range": "± 14.8409",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 2.21231,
            "range": "± 558.798",
            "unit": "us",
            "extra": "50 samples\n16 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 384.207,
            "range": "± 76.4101",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.57007,
            "range": "± 20.2494",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 277.786,
            "range": "± 13.6176",
            "unit": "ns",
            "extra": "50 samples\n114 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 861.322,
            "range": "± 51.1374",
            "unit": "ns",
            "extra": "50 samples\n37 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 279.212,
            "range": "± 3.97757",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 1.0659,
            "range": "± 52.8118",
            "unit": "us",
            "extra": "50 samples\n30 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 376.31,
            "range": "± 68.3085",
            "unit": "ns",
            "extra": "50 samples\n85 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 279.138,
            "range": "± 9.95005",
            "unit": "ns",
            "extra": "50 samples\n114 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 377.61,
            "range": "± 87.4117",
            "unit": "ns",
            "extra": "50 samples\n85 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 395.287,
            "range": "± 72.695",
            "unit": "ns",
            "extra": "50 samples\n82 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 278.635,
            "range": "± 15.4346",
            "unit": "ns",
            "extra": "50 samples\n115 iterations"
          },
          {
            "name": "delta encode",
            "value": 31.562,
            "range": "± 1.98991",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 43.4941,
            "range": "± 1.7765",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 29.1536,
            "range": "± 1.0049",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 57.3011,
            "range": "± 2.06697",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.1553,
            "range": "± 2.17488",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 70.3917,
            "range": "± 3.08485",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 31.6435,
            "range": "± 2.13965",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.93302,
            "range": "± 199.362",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 29.1653,
            "range": "± 1.04802",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.1772,
            "range": "± 1.07823",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 14.2311,
            "range": "± 92.8056",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 126.659,
            "range": "± 291.199",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 127.334,
            "range": "± 271.856",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.7875,
            "range": "± 75.596",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 13.1343,
            "range": "± 37.7424",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 8.12225,
            "range": "± 0.417172",
            "unit": "ns",
            "extra": "50 samples\n3803 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 381.165,
            "range": "± 78.2479",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 12.5106,
            "range": "± 633.877",
            "unit": "us",
            "extra": "50 samples\n3 iterations"
          },
          {
            "name": "as_tensor",
            "value": 34.1735,
            "range": "± 2.4811",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 25.9975,
            "range": "± 1.4176",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "00076e57002bfb4cca40717b0bc23495d9938ff7",
          "message": "fix: MSVC OPTIONAL macro (sal.h) + MmapParquetReader guard on Windows",
          "timestamp": "2026-03-09T15:21:45Z",
          "tree_id": "b3b6b5635bc04a23097736459a2a5c5eff9f9806",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/00076e57002bfb4cca40717b0bc23495d9938ff7"
        },
        "date": 1773069860761,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 757.594,
            "range": "± 22.7102",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 765.033,
            "range": "± 25.5293",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3694.4300000000003,
            "range": "± 24225.8",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 12970.3,
            "range": "± 82661.6",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 893.528,
            "range": "± 24.491",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 132.318,
            "range": "± 29.6883",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.8369,
            "range": "± 126.585",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.3466,
            "range": "± 84.3855",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 148.774,
            "range": "± 31.5018",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 187.293,
            "range": "± 49.4126",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 337.026,
            "range": "± 62.0578",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.12888,
            "range": "± 489.164",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 328.574,
            "range": "± 13.5347",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.63641,
            "range": "± 239.238",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 350.351,
            "range": "± 69.3974",
            "unit": "ns",
            "extra": "50 samples\n77 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.2964,
            "range": "± 16.4075",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 242.253,
            "range": "± 13.6392",
            "unit": "ns",
            "extra": "50 samples\n122 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 750.523,
            "range": "± 29.7402",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 240.657,
            "range": "± 3.42008",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.8208970000000001,
            "range": "± 0.044816800000000004",
            "unit": "us",
            "extra": "50 samples\n36 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 341.282,
            "range": "± 57.9879",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 240.412,
            "range": "± 9.58501",
            "unit": "ns",
            "extra": "50 samples\n121 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 341.605,
            "range": "± 63.6008",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 361.302,
            "range": "± 68.4983",
            "unit": "ns",
            "extra": "50 samples\n76 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 235.482,
            "range": "± 12.283",
            "unit": "ns",
            "extra": "50 samples\n125 iterations"
          },
          {
            "name": "delta encode",
            "value": 35.3264,
            "range": "± 2.09241",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 41.7288,
            "range": "± 1.49341",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 29.2546,
            "range": "± 5.06579",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 51.9943,
            "range": "± 2.7604",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 52.4912,
            "range": "± 1.70266",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 66.7553,
            "range": "± 4.14239",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 34.8986,
            "range": "± 1.93112",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.75529,
            "range": "± 169.519",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 30.5958,
            "range": "± 4.14549",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 29.6903,
            "range": "± 4.74261",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1183,
            "range": "± 107.338",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 124.802,
            "range": "± 332.211",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 124.468,
            "range": "± 238.11",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.5101,
            "range": "± 119.78",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.5403,
            "range": "± 122.832",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.01392,
            "range": "± 0.353366",
            "unit": "ns",
            "extra": "50 samples\n4148 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 391.896,
            "range": "± 76.9117",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.3471,
            "range": "± 2.07139",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.4091,
            "range": "± 2.00057",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 24.9686,
            "range": "± 1.93858",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "committer": {
            "email": "ogundeji.ja@gmail.com",
            "name": "Johnson Ogundeji",
            "username": "Johnson-Ogundeji"
          },
          "distinct": true,
          "id": "1840199765232d64e9630ae8474ea132f16053e9",
          "message": "fix: RLE decoder OOM on bit_width==0 — cap group_count + value limit (CWE-770)",
          "timestamp": "2026-03-09T16:28:36Z",
          "tree_id": "dd1deefd2d55a7fbf75951847c0687582b7e59ee",
          "url": "https://github.com/SIGNETSTACK/SIGNET_FORGE/commit/1840199765232d64e9630ae8474ea132f16053e9"
        },
        "date": 1773073867970,
        "tool": "catch2",
        "benches": [
          {
            "name": "write",
            "value": 760.375,
            "range": "± 30.8221",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 767.984,
            "range": "± 20.3663",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 3680.81,
            "range": "± 21756.6",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 12966.300000000001,
            "range": "± 35368.7",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "write",
            "value": 888.149,
            "range": "± 26.6601",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<double> price",
            "value": 126.762,
            "range": "± 24.1243",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_all",
            "value": 22.7929,
            "range": "± 272.267",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_columns price+qty",
            "value": 19.0804,
            "range": "± 82.7138",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "read_column<int64_t> ts",
            "value": 132.802,
            "range": "± 27.7152",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "open + num_rows",
            "value": 174.989,
            "range": "± 34.5372",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append 32B",
            "value": 335.558,
            "range": "± 50.8287",
            "unit": "ns",
            "extra": "50 samples\n89 iterations"
          },
          {
            "name": "append 256B",
            "value": 1.06196,
            "range": "± 130.264",
            "unit": "us",
            "extra": "50 samples\n28 iterations"
          },
          {
            "name": "1000 appends",
            "value": 327.227,
            "range": "± 7.04932",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "append + flush(no-fsync)",
            "value": 1.63405,
            "range": "± 211.667",
            "unit": "us",
            "extra": "50 samples\n19 iterations"
          },
          {
            "name": "manager append 32B",
            "value": 363.471,
            "range": "± 152.145",
            "unit": "ns",
            "extra": "50 samples\n78 iterations"
          },
          {
            "name": "read_all 10K records",
            "value": 2.3147,
            "range": "± 106.112",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 236.442,
            "range": "± 9.01588",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "mmap append 256B",
            "value": 747.338,
            "range": "± 43.0268",
            "unit": "ns",
            "extra": "50 samples\n39 iterations"
          },
          {
            "name": "mmap 1000 appends",
            "value": 236.13,
            "range": "± 3.75748",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "mmap append + flush(no-msync)",
            "value": 0.805524,
            "range": "± 0.049537",
            "unit": "us",
            "extra": "50 samples\n37 iterations"
          },
          {
            "name": "fwrite append 32B",
            "value": 340.202,
            "range": "± 44.4868",
            "unit": "ns",
            "extra": "50 samples\n87 iterations"
          },
          {
            "name": "mmap append 32B",
            "value": 235.846,
            "range": "± 9.1552",
            "unit": "ns",
            "extra": "50 samples\n124 iterations"
          },
          {
            "name": "WalWriter append 32B",
            "value": 349.78,
            "range": "± 54.4591",
            "unit": "ns",
            "extra": "50 samples\n85 iterations"
          },
          {
            "name": "WalManager append 32B",
            "value": 361.03,
            "range": "± 70.3059",
            "unit": "ns",
            "extra": "50 samples\n75 iterations"
          },
          {
            "name": "WalMmapWriter append 32B",
            "value": 239.436,
            "range": "± 9.50767",
            "unit": "ns",
            "extra": "50 samples\n123 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.5443,
            "range": "± 3.56261",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta decode",
            "value": 41.8573,
            "range": "± 1.68779",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "bss encode",
            "value": 34.1648,
            "range": "± 8.7145",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 33.3666,
            "range": "± 9.6826",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "rle encode bit_width=1",
            "value": 53.8986,
            "range": "± 2.08866",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "rle decode bit_width=1",
            "value": 70.4932,
            "range": "± 2.19561",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "delta encode",
            "value": 36.6066,
            "range": "± 2.27269",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "plain copy baseline",
            "value": 2.84126,
            "range": "± 173.276",
            "unit": "us",
            "extra": "50 samples\n11 iterations"
          },
          {
            "name": "bss encode",
            "value": 33.2169,
            "range": "± 8.33944",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "bss decode",
            "value": 32.4137,
            "range": "± 9.07364",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "write_batch 10K",
            "value": 13.1416,
            "range": "± 103.057",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "get latest",
            "value": 127.47,
            "range": "± 264.057",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of mid-range",
            "value": 127.768,
            "range": "± 164.869",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "as_of_batch 100 entities",
            "value": 12.7631,
            "range": "± 96.2682",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "history 100 records",
            "value": 12.7744,
            "range": "± 145.827",
            "unit": "ms",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push+pop",
            "value": 7.07771,
            "range": "± 0.449959",
            "unit": "ns",
            "extra": "50 samples\n4123 iterations"
          },
          {
            "name": "4P4C 4000 items",
            "value": 381.751,
            "range": "± 90.5309",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "push 1000 rows + column_view",
            "value": 26.2469,
            "range": "± 1.05307",
            "unit": "us",
            "extra": "50 samples\n2 iterations"
          },
          {
            "name": "as_tensor",
            "value": 36.7851,
            "range": "± 2.19454",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          },
          {
            "name": "publish+pop 1000",
            "value": 36.3113,
            "range": "± 4.25881",
            "unit": "us",
            "extra": "50 samples\n1 iterations"
          }
        ]
      }
    ]
  }
}