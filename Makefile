# Build the nine C programs (Linux or WSL). V3 uses pthread_setaffinity_np,
# which is a GNU/Linux extension, so this Makefile is meant to be run there.
CC      = gcc
CFLAGS  = -O2 -Wall
PTHREAD = -pthread
MATH    = -lm
BIN     = bin

T1 = src/task1_susy_logistic
T2 = src/task2_spmv
T3 = src/task3_bfs

.PHONY: all clean dirs task1 task2 task3

all: task1 task2 task3

dirs:
	mkdir -p $(BIN)

task1: dirs $(BIN)/task1_v1_serial $(BIN)/task1_v2_pthreads $(BIN)/task1_v3_affinity
task2: dirs $(BIN)/task2_v1_serial $(BIN)/task2_v2_pthreads $(BIN)/task2_v3_affinity
task3: dirs $(BIN)/task3_v1_serial $(BIN)/task3_v2_pthreads $(BIN)/task3_v3_affinity

$(BIN)/task1_v1_serial: $(T1)/task1_v1_serial.c | dirs
	$(CC) $(CFLAGS) $< $(MATH) -o $@

$(BIN)/task1_v2_pthreads: $(T1)/task1_v2_pthreads.c | dirs
	$(CC) $(CFLAGS) $< $(MATH) $(PTHREAD) -o $@

$(BIN)/task1_v3_affinity: $(T1)/task1_v3_affinity.c | dirs
	$(CC) $(CFLAGS) $< $(MATH) $(PTHREAD) -o $@

$(BIN)/task2_v1_serial: $(T2)/task2_v1_serial.c | dirs
	$(CC) $(CFLAGS) $< $(MATH) -o $@

$(BIN)/task2_v2_pthreads: $(T2)/task2_v2_pthreads.c | dirs
	$(CC) $(CFLAGS) $< $(MATH) $(PTHREAD) -o $@

$(BIN)/task2_v3_affinity: $(T2)/task2_v3_affinity.c | dirs
	$(CC) $(CFLAGS) $< $(MATH) $(PTHREAD) -o $@

$(BIN)/task3_v1_serial: $(T3)/task3_v1_serial.c | dirs
	$(CC) $(CFLAGS) $< -o $@

$(BIN)/task3_v2_pthreads: $(T3)/task3_v2_pthreads.c | dirs
	$(CC) $(CFLAGS) $< $(PTHREAD) -o $@

$(BIN)/task3_v3_affinity: $(T3)/task3_v3_affinity.c | dirs
	$(CC) $(CFLAGS) $< $(PTHREAD) -o $@

clean:
	rm -rf $(BIN) gmon.out
