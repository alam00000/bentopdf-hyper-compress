#include <stdio.h>
#include "hpdf.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s in.pdf out.pdf\n", argv[0]);
    return 2;
  }
  int ids[]  = {1, 2, 3, 10};
  int vals[] = {150, 70, 4, 1};
  int rc = hpdf_compress_file(argv[1], argv[2], ids, vals, 4, "");
  printf("compress rc=%d (0=ok, 1=fail, 2=signed) version=%s\n", rc, hpdf_version());
  if (rc == 1) {
    fprintf(stderr, "error: %s\n", hpdf_last_error());
  }
  return rc == 1 ? 1 : 0;
}
