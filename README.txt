1. To Compile:
make

2. To Run:
Parallel: ./parallel_client --parallel "Tom Hanks" 3
Sequential: ./parallel_client --sequential "Tom Hanks" 3

3. Clean:
make clean

Notes:
Parallel uses 8 worker threads by default
Shows nodes visited per level and total execution time