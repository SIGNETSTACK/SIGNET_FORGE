window.BENCHMARK_DATA = {
  "lastUpdate": 1772866061640,
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
      }
    ]
  }
}