#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hpdf.h"

#define THREADS 8
#define ROUNDS 4

static unsigned char* g_input;
static unsigned long g_input_size;
static unsigned char* g_expected;
static unsigned long g_expected_size;
static int g_ids[] = {1, 2, 3};
static int g_vals[] = {150, 50, 4};

static void* worker(void* arg) {
  long id = (long)arg;
  for (int r = 0; r < ROUNDS; ++r) {
    if (id % 2) {
      unsigned long size = 0;
      int status = 0;
      unsigned char* out = hpdf_compress_buffer((const unsigned char*)"not a pdf", 9,
                                                g_ids, g_vals, 3, "", &size, &status);
      if (out || status != 1 || strstr(hpdf_last_error(), "cannot parse") == NULL) {
        fprintf(stderr, "thread %ld: bad input was not reported as its own error\n", id);
        return (void*)1;
      }
    }
    unsigned long size = 0;
    int status = 0;
    unsigned char* out = hpdf_compress_buffer(g_input, g_input_size, g_ids, g_vals, 3, "",
                                              &size, &status);
    if (!out || status == 1 || hpdf_last_error()[0] != '\0') {
      fprintf(stderr, "thread %ld: compress failed: %s\n", id, hpdf_last_error());
      return (void*)1;
    }
    int same = size == g_expected_size && memcmp(out, g_expected, size) == 0;
    hpdf_free(out);
    if (!same) {
      fprintf(stderr, "thread %ld: output differs from the single-threaded result\n", id);
      return (void*)1;
    }
  }
  return NULL;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s in.pdf\n", argv[0]);
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  g_input_size = (unsigned long)ftell(f);
  fseek(f, 0, SEEK_SET);
  g_input = malloc(g_input_size);
  if (fread(g_input, 1, g_input_size, f) != g_input_size) return 2;
  fclose(f);

  int status = 0;
  g_expected = hpdf_compress_buffer(g_input, g_input_size, g_ids, g_vals, 3, "",
                                    &g_expected_size, &status);
  if (!g_expected) {
    fprintf(stderr, "single-threaded compress failed: %s\n", hpdf_last_error());
    return 1;
  }

  pthread_t t[THREADS];
  for (long i = 0; i < THREADS; ++i) pthread_create(&t[i], NULL, worker, (void*)i);
  int failed = 0;
  for (int i = 0; i < THREADS; ++i) {
    void* rc;
    pthread_join(t[i], &rc);
    failed |= rc != NULL;
  }
  hpdf_free(g_expected);
  free(g_input);
  printf("%s: %d threads x %d rounds, %lu -> %lu bytes each\n", failed ? "FAIL" : "ok", THREADS,
         ROUNDS, g_input_size, g_expected_size);
  return failed;
}
