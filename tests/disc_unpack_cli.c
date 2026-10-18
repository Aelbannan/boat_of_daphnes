// bluewake_disc_unpack IN OUT: the disc unpacker (runtime/host/src/disc_unpack.c) on one image. With a third argument, also
// runs the disc importer's check on the result. Exit 0 on success, 1 when the image is one it does not unpack, 2 on a failure.
#include <stdio.h>
#include <string.h>

#include "disc_import.h"
#include "disc_unpack.h"

static void progress(void* context, double fraction, const char* stage) {
    (void)context;
    fprintf(stderr, "\r%s: %5.1f%%", stage != NULL ? stage : "", fraction * 100.0);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: bluewake_disc_unpack IN OUT [check]\n");
        return 2;
    }
    char error[1024] = "";
    const int rc = bluewake_disc_unpack(argv[1], argv[2], progress, NULL, error, sizeof error);
    fprintf(stderr, "\n");
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", rc == 1 ? "not handled" : "failed", error);
        return rc == 1 ? 1 : 2;
    }
    if (argc > 3 && bluewake_disc_check(argv[2], error, sizeof error) != 0) {
        fprintf(stderr, "check failed: %s\n", error);
        return 2;
    }
    printf("ok\n");
    return 0;
}
